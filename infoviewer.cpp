#include <atomic>
#include <mosquitto.h>
#include <mutex>
#include <signal.h>
#include <stdarg.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <SDL/SDL.h>
#include <SDL/SDL_gfxPrimitives.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>

std::atomic_bool do_exit { false };

void sigh(int s)
{
	do_exit = true;
}

std::string myformat(const char *const fmt, ...)
{
	char *buffer = nullptr;
        va_list ap;

        va_start(ap, fmt);
        if (vasprintf(&buffer, fmt, ap) == -1) {
		va_end(ap);
		return "(?)";
	}
        va_end(ap);

	std::string result = buffer;
	free(buffer);

	return result;
}

void set_thread_name(const std::string & name)
{
	std::string full_name = "IV:" + name;

	if (full_name.length() > 15)
		full_name = full_name.substr(0, 15);

	pthread_setname_np(pthread_self(), full_name.c_str());
}

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
	const int max_width { 0 };
	std::mutex lock;
	std::vector<SDL_Surface *> surfaces;
	std::string text;
	int total_w { 0 }, h { 0 };
	SDL_Color col { 0, 0, 0, 0 };

public:
	container(const std::string & font_file, const int font_height, const int max_width) : max_width(max_width)
	{
		font = load_font(font_file, font_height, true);
	}

	virtual ~container()
	{
		for(auto & s : surfaces)
			SDL_FreeSurface(s);
	}

	virtual std::pair<int, int> set_text(const std::vector<std::string> & in)
	{
		std::vector<SDL_Surface *> temp_new;

		// new text?
		std::string new_text;
		for(auto t : in)
			new_text += t;

		if (new_text == text)
			// no; don't re-render
			return { total_w, h };

		// render new text
		int new_total_w = 0, new_h = 0;

		ttf_lock.lock();
		for(auto text : in) {
			while(!text.empty()) {
				std::string part = text.substr(0, 10);
				text.erase(0, 10);

				SDL_Surface *new_ = TTF_RenderUTF8_Solid(font, part.c_str(), col);
				temp_new.push_back(new_);
				new_total_w += new_->w;
				new_h = std::max(new_h, new_->h);
			}
		}
		ttf_lock.unlock();

		std::vector<SDL_Surface *> old;

		lock.lock();

		old = surfaces;

		surfaces = temp_new;
		total_w = new_total_w;
		h = new_h;

		lock.unlock();

		for(auto & s : old)
			SDL_FreeSurface(s);

		return { total_w, h };
	}
};

class text_box : public container
{
public:
	text_box(const std::string & font_file, const int font_height, const int r, const int g, const int b, const int max_width) : container(font_file, font_height, max_width)
	{
		col.r = r;
		col.g = g;
		col.b = b;
	}

	virtual ~text_box()
	{
	}

	void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center)
	{
		lock.lock();

		int biggest_w = 0;
		for(auto & p : surfaces)
			biggest_w = std::max(biggest_w, p->w);

		const int put_x = x * sd->xsteps + 1;
		int put_y = y * sd->ysteps + 1;
		const int put_w = w * sd->xsteps - 2;
		int work_h = h * sd->ysteps - 2;
		for(auto & p : surfaces) {
			SDL_Rect dest { center ? put_x + put_w / 2 - biggest_w / 2 : put_x, put_y, put_w, p->h };
			SDL_Rect src { 0, 0, p->w, p->h };

			SDL_BlitSurface(p, &src, sd->screen, &dest);

			put_y += p->h;
			work_h -= p->h;
			if (work_h <= 0)
				break;
		}

		lock.unlock();
	}
};

class scroller : public container
{
private:
	int render_x { 0 };
	std::thread *th { nullptr };
	std::string text_seperator;

public:
	scroller(const std::string & font_file, const int font_height, const int r, const int g, const int b, const std::string & text_seperator, const int max_width) : container(font_file, font_height, max_width), text_seperator(text_seperator)
	{
		col.r = r;
		col.g = g;
		col.b = b;

		th = new std::thread(std::ref(*this));
	}

	virtual ~scroller()
	{
		th->join();
		delete th;
	}

	std::pair<int, int> set_text(const std::vector<std::string> & in) override
	{
		if (text_seperator.empty())
			return container::set_text(in);
	
		std::vector<std::string> copy = in;
		copy.push_back(text_seperator);

		return container::set_text(copy);
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h)
	{
		lock.lock();

		if (!surfaces.empty()) {
			SDL_Rect dest { x * sd->xsteps + 1, y * sd->ysteps + 1, sd->xsteps * put_w - 2, sd->ysteps * put_h - 2 };
			int cur_render_x = render_x;
			int pixels_to_do = sd->xsteps * put_w - 2;

			do {
				for(auto & p : surfaces) {
					if (p->w <= cur_render_x) {
						cur_render_x -= p->w;
						continue;
					}

					SDL_Rect cur_src { cur_render_x, 0, p->w - cur_render_x, h };
					cur_render_x = 0;

					SDL_BlitSurface(p, &cur_src, sd->screen, &dest);
					dest.x += cur_src.w;
					pixels_to_do -= cur_src.w;

					if (pixels_to_do <= 0)
						break;
				}
			}
			while(pixels_to_do > 0);
		}

		lock.unlock();
	}

	void operator()() {
		set_thread_name("scroller");

		for(;!do_exit;) {
			usleep(10000);

			if (total_w) {
				lock.lock();

				render_x++;
				render_x %= total_w;

				lock.unlock();
			}
		}
	}
};

void on_message(struct mosquitto *, void *arg, const struct mosquitto_message *msg, const mosquitto_property *)
{
	container *c = (container *)arg;

	std::string new_text((const char *)msg->payload, msg->payloadlen);

	auto result = c->set_text({ new_text });
	printf("on_message: %s (%dx%d)\n", new_text.c_str(), result.first, result.second);
}

class mqtt_feed
{
private:
	std::thread *th { nullptr };
	struct mosquitto *mi { nullptr };
	static int nr;

public:
	mqtt_feed(const std::string & host, const int port, const std::string & topic, container *const c)
	{
		mi = mosquitto_new(myformat("infoviewer-%d", nr++).c_str(), true, c);

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
		set_thread_name("mqtt");

		for(;!do_exit;)
			mosquitto_loop(mi, 11000, 1);
	}
};

int mqtt_feed::nr = 0; 

int main(int argc, char *argv[])
{
	signal(SIGTERM, sigh);

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

	scroller s("/usr/share/vlc/skins2/fonts/FreeSans.ttf", ysteps * 5, 255, 255, 255, " *** ", w);
	mqtt_feed mfs("mauer", 1883, "vanheusden/sensors/huiskamer/co2", &s);

	text_box t("/usr/share/vlc/skins2/fonts/FreeSans.ttf", ysteps * 8, 0, 0, 0, 16 * xsteps);
	mqtt_feed mft("mauer", 1883, "minecraft-user-count", &t);

	for(;!do_exit;) {
		if (grid) {
			for(int cy=0; cy<n_rows; cy++)
				lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);

			for(int cx=0; cx<n_columns; cx++)
				lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
		}

		draw_box(&sd, 0, 0, 16, 8, true, 80, 255, 80);
		t.put_static(&sd, 0, 0, 16, 8, true);

		draw_box(&sd, 0, 20, 80, 5, true, 80, 80, 255);
		s.put_scroller(&sd, 0, 20, 80, 5);

		SDL_UpdateRect(screen, 0, 0, w, h);

		usleep(10000);

		SDL_Event event { 0 };
		if (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				break;
		}
	}

	return 0;
}
