#include <mosquitto.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <SDL/SDL.h>
#include <SDL/SDL_gfxPrimitives.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>

typedef struct
{
	SDL_Surface *screen;
	int scr_w, scr_h;
	int xsteps, ysteps;
} screen_descriptor_t;

void draw_box(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool border, const int r, const int g, const int b)
{
	int x1 = x * sd->xsteps;
	int y1 = y * sd->ysteps;
	int x2 = x1 + sd->xsteps * w - 1;
	int y2 = y1 + sd->ysteps * h - 1;

	boxRGBA(sd->screen, x1, y1, x2, y2, r, g, b, 255);
	rectangleRGBA(sd->screen, x1, y1, x2, y2, r, g, b, 191);
}

std::mutex ttf_lock;
TTF_Font * load_font(const std::string & filename, unsigned int font_height, bool fast_rendering)
{
        char *const real_path = realpath(filename.c_str(), NULL);

        ttf_lock.lock();

        TTF_Font *font = TTF_OpenFont(real_path, font_height);
	if (!font)
		fprintf(stderr, "font %s (%s): %s\n", filename.c_str(), real_path, TTF_GetError());

        if (!fast_rendering)
                TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

        ttf_lock.unlock();

        free(real_path);

        return font;
}

class container
{
private:
	TTF_Font *font { nullptr };

protected:
	std::mutex lock;
	SDL_Surface *surface { nullptr };
	SDL_Color col { 0, 0, 0, 0 };

public:
	container(const std::string & font_file, const int font_height)
	{
		font = load_font(font_file, font_height, true);
	}

	virtual ~container()
	{
		SDL_FreeSurface(surface);
	}

	virtual void set_text(const std::string & text)
	{
		ttf_lock.lock();
		SDL_Surface *temp_new = TTF_RenderUTF8_Solid(font, text.c_str(), col);
		ttf_lock.unlock();

		if (temp_new) {
			lock.lock();
			SDL_Surface *temp_prev = surface;
			surface = temp_new;
			lock.unlock();

			SDL_FreeSurface(temp_prev);
		}
		else {
			printf("Failed to render \"%s\": %s\n", text.c_str(), TTF_GetError());
		}
	}
};

class text_box : public container
{
private:
	SDL_Rect pos { 0, 0, 0, 0 };

public:
	text_box(const std::string & font_file, const int font_height, const int r, const int g, const int b) : container(font_file, font_height)
	{
		col.r = r;
		col.g = g;
		col.b = b;
	}

	virtual ~text_box()
	{
	}

	void set_text(const std::string & text) override
	{
		container::set_text(text);

		lock.lock();
		pos.w = surface->w;
		pos.h = surface->h;
		lock.unlock();
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h)
	{
		lock.lock();

		if (surface && pos.w && pos.h) {
			const int end_x = x * sd->xsteps + w * sd->xsteps;

			SDL_Rect dest { x * sd->xsteps + 1, y * sd->ysteps + 1, sd->xsteps * w - 2, sd->ysteps * h - 2 };

			SDL_Rect pos_work = pos;

			while(dest.x < end_x) {
				SDL_BlitSurface(surface, &pos_work, sd->screen, &dest);
				dest.x += pos_work.w - pos_work.x;
				pos_work.x = 0;
			}
		}

		lock.unlock();
	}
};

class scroller : public container
{
private:
	SDL_Rect pos { 0, 0, 0, 0 };
	std::thread *th { nullptr };
	std::string text_seperator;

public:
	scroller(const std::string & font_file, const int font_height, const int r, const int g, const int b, const std::string & text_seperator) : container(font_file, font_height), text_seperator(text_seperator)
	{
		col.r = r;
		col.g = g;
		col.b = b;

		th = new std::thread(std::ref(*this));
	}

	virtual ~scroller()
	{
		delete th;
	}

	void set_text(const std::string & text) override
	{
		container::set_text(text + text_seperator);

		lock.lock();
		if (surface) {
			pos.w = surface->w;
			pos.h = surface->h;
		}
		lock.unlock();
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h)
	{
		lock.lock();

		if (surface && pos.w && pos.h) {
			const int end_x = x * sd->xsteps + w * sd->xsteps;

			SDL_Rect dest { x * sd->xsteps + 1, y * sd->ysteps + 1, sd->xsteps * w - 2, sd->ysteps * h - 2 };

			SDL_Rect pos_work = pos;

			while(dest.x < end_x) {
				SDL_BlitSurface(surface, &pos_work, sd->screen, &dest);
				dest.x += pos_work.w - pos_work.x;
				pos_work.x = 0;
			}
		}

		lock.unlock();
	}

	void operator()() {
		for(;;) {
			usleep(10000);

			if (pos.w) {
				lock.lock();

				pos.x++;
				pos.x %= pos.w;

				lock.unlock();
			}
		}
	}
};

void on_message(struct mosquitto *, void *arg, const struct mosquitto_message *msg, const mosquitto_property *)
{
	container *c = (container *)arg;

	std::string new_text((const char *)msg->payload, msg->payloadlen);
	printf("on_message: %s\n", new_text.c_str());

	c->set_text(new_text);
}

class mqtt_feed
{
private:
	std::thread *th { nullptr };
	struct mosquitto *mi { nullptr };

public:
	mqtt_feed(const std::string & host, const int port, const std::string & topic, container *const c)
	{
		mi = mosquitto_new("infoviewer", true, c);

		mosquitto_connect(mi, host.c_str(), port, 30);

		mosquitto_message_v5_callback_set(mi, on_message);

		mosquitto_subscribe(mi, 0, topic.c_str(), 0);

		th = new std::thread(std::ref(*this));
	}

	virtual ~mqtt_feed()
	{
	}

	void operator()()
	{
		for(;;)
			mosquitto_loop(mi, 11000, 1);
	}
};

int main(int argc, char *argv[])
{
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);

	TTF_Init();

	mosquitto_lib_init();

	int n_columns = 80, n_rows = 25;
	bool grid = true;
	bool full_screen = false;

	int create_w = 1440, create_h = 800;

	const SDL_VideoInfo *svi = SDL_GetVideoInfo();
	SDL_Surface *screen = SDL_SetVideoMode(create_w, create_h, 32, (svi->hw_available ? SDL_HWSURFACE : SDL_SWSURFACE) | SDL_ASYNCBLIT | (full_screen ? SDL_FULLSCREEN : 0));
	const int w = screen->w;
	const int h = screen->h;
	const int xsteps = w / n_columns;
	const int ysteps = h / n_rows;
	screen_descriptor_t sd { screen, w, h, xsteps, ysteps };

	scroller s("/usr/share/vlc/skins2/fonts/FreeSans.ttf", ysteps * 5, 255, 255, 255, " *** ");

	mqtt_feed mf("mauer", 1883, "dak/update", &s);

	for(;;) {
		if (grid) {
			for(int cy=0; cy<n_rows; cy++)
				lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);

			for(int cx=0; cx<n_columns; cx++)
				lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
		}

		draw_box(&sd, 0, 0, 16, 8, true, 80, 255, 80);

		draw_box(&sd, 0, 20, 80, 5, true, 80, 80, 255);
		s.put_scroller(&sd, 0, 20, 80, 5);

		SDL_UpdateRect(screen, 0, 0, w, h);

		usleep(10000);
	}

	return 0;
}
