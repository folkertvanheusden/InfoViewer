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

class scroller
{
private:
	std::mutex lock;
	SDL_Surface *surface { nullptr };
	SDL_Rect pos { 0, 0, 0, 0 };
	TTF_Font *font { nullptr };
	std::thread *th { nullptr };
	SDL_Color col { 0, 0, 0, 0 };

public:
	scroller(const std::string & font_file, const int font_height, const int r, const int g, const int b)
	{
		col.r = r;
		col.g = g;
		col.b = b;

		font = load_font(font_file, font_height, true);

		th = new std::thread(std::ref(*this));
	}

	~scroller()
	{
		delete th;
	}

	void set_text(const std::string & text)
	{
		ttf_lock.lock();
		SDL_Surface *temp_new = TTF_RenderUTF8_Solid(font, text.c_str(), col);
		ttf_lock.unlock();

		lock.lock();
		SDL_Surface *temp_prev = surface;
		surface = temp_new;
		pos.w = surface->w,
		pos.h = surface->h,
		lock.unlock();

		SDL_FreeSurface(temp_prev);
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h)
	{
		lock.lock();

		if (surface) {
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

			lock.lock();
			pos.x++;
			pos.x %= pos.w;
			lock.unlock();
		}
	}
};

int main(int argc, char *argv[])
{
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);

	TTF_Init();

	int n_columns = 80, n_rows = 25;
	bool grid = true;

	const SDL_VideoInfo *svi = SDL_GetVideoInfo();
	SDL_Surface *screen = SDL_SetVideoMode(0, 0, 32, (svi->hw_available ? SDL_HWSURFACE : SDL_SWSURFACE) | SDL_ASYNCBLIT);
	const int w = screen->w;
	const int h = screen->h;
	const int xsteps = w / n_columns;
	const int ysteps = h / n_rows;
	screen_descriptor_t sd { screen, w, h, xsteps, ysteps };

	scroller s("/usr/share/vlc/skins2/fonts/FreeSans.ttf", ysteps * 5, 255, 255, 255);
	s.set_text("Dit is een test.");

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
