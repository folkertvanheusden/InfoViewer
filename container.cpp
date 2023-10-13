#include <atomic>
#include <cassert>
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "container.h"
#include "error.h"
#include "formatters.h"
#include "str.h"


extern std::atomic_bool do_exit;

extern std::mutex ttf_lock;

extern void set_thread_name(const std::string & name);

TTF_Font * load_font(const std::string & filename, unsigned int font_height, bool fast_rendering)
{
        char *const real_path = realpath(filename.c_str(), NULL);

        ttf_lock.lock();

        TTF_Font *font = TTF_OpenFont(real_path, font_height);
	if (!font)
		error_exit(false, "font %s (%s) can't be loaded: %s\n", filename.c_str(), real_path, TTF_GetError());

        if (!fast_rendering)
                TTF_SetFontHinting(font, TTF_HINTING_LIGHT);

        ttf_lock.unlock();

        free(real_path);

        return font;
}

container::container(SDL_Renderer *const renderer, const std::string & font_file, const int font_height, const int max_width, base_text_formatter *const fmt, const int clear_after) : renderer(renderer), max_width(max_width), fmt(fmt), clear_after(clear_after)
{
	assert(renderer);

	font = load_font(font_file, font_height, true);

	th = new std::thread(std::ref(*this));
}

container::~container()
{
	for(auto & s : surfaces)
		SDL_DestroyTexture(s);

	th->join();
	delete th;
}

void container::operator()()
{
	if (clear_after != -1) {
		while(!do_exit) {
			usleep(500000);

			time_t now = time(nullptr);

			lock.lock();
			if (most_recent_update != 0 && now - most_recent_update >= clear_after) {
				std::vector<SDL_Texture *> old = surfaces;
				surfaces.clear();

				total_w = 0;
				h       = 0;

				most_recent_update = 0;

				lock.unlock();

				for(auto & s : old)
					SDL_DestroyTexture(s);
			}
			else {
				lock.unlock();
			}
		}
	}
}

std::pair<int, int> container::set_text(const std::vector<std::string> & in_)
{
	std::vector<SDL_Texture *> temp_new;

	std::vector<std::string> in;

	// new text?
	std::string new_text;
	for(auto t : in_) {
		auto new_t = fmt ? fmt->process(t) : t;
		new_text += new_t;

		std::size_t lf = new_t.find("\n");
		if (lf != std::string::npos) {
			std::vector<std::string> parts = split(new_t, "\n");

			std::copy(parts.begin(), parts.end(), std::back_inserter(in));
		}
		else {
			in.push_back(new_t);
		}
	}

	if (new_text == text)
		// no; don't re-render
		return { total_w, h };

	// render new text
	int new_total_w = 0, new_h = 0;

	ttf_lock.lock();
	for(auto line : in) {
		int text_w = 0;
		int dummy  = 0;
		TTF_SizeUTF8(font, line.c_str(), &text_w, &dummy);

		int    divider = ceil(double(text_w) / max_width);
		size_t n_chars = ceil(line.size() / double(divider));

		for(int i=0; i<divider; i++) {
			std::string part = line.substr(0, n_chars);
			if (part.empty())
				break;

			line.erase(0, n_chars);

			SDL_Surface *new_s = TTF_RenderUTF8_Blended(font, part.c_str(), col);
			assert(new_s);
			SDL_Texture *new_t = SDL_CreateTextureFromSurface(renderer, new_s);
			assert(new_t);

			temp_new.push_back(new_t);
			new_total_w += new_s->w;
			new_h = std::max(new_h, new_s->h);

			SDL_FreeSurface(new_s);
		}
	}
	ttf_lock.unlock();

	lock.lock();

	std::vector<SDL_Texture *> old = surfaces;

	surfaces = temp_new;
	total_w = new_total_w;
	h = new_h;

	most_recent_update = time(nullptr);

	lock.unlock();

	for(auto & s : old)
		SDL_DestroyTexture(s);

	return { total_w, h };
}

text_box::text_box(SDL_Renderer * const renderer, const std::string & font_file, const int font_height, const int r, const int g, const int b, const int max_width, base_text_formatter *const fmt, const int clear_after) : container(renderer, font_file, font_height, max_width, fmt, clear_after)
{
	col.r = r;
	col.g = g;
	col.b = b;
}

text_box::~text_box()
{
}

void text_box::put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h)
{
	assert(0);
}

void text_box::put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center_h, const bool center_v)
{
	lock.lock();

	int biggest_w = 0;
	int biggest_h = 0;

	for(auto & p : surfaces) {
		Uint32 format = 0;
		int    access = 0;
		int    w      = 0;
		int    h      = 0;

		int    rc     = SDL_QueryTexture(p, &format, &access, &w, &h);
		assert(rc == 0);

		biggest_w = std::max(biggest_w, w);
		biggest_h = std::max(biggest_h, h);
	}

	const int put_x  = x * sd->xsteps + 1;
	int       put_y  = y * sd->ysteps + 1;

	const int put_w  = w * sd->xsteps - 2;
	int       work_h = h * sd->ysteps - 2;

	for(auto & p : surfaces) {
		Uint32 format = 0;
		int    access = 0;
		int    w      = 0;
		int    h      = 0;
		int    rc     = SDL_QueryTexture(p, &format, &access, &w, &h);
		assert(rc == 0);

		int     cur_x = center_h ? put_x + put_w / 2 - w / 2 : put_x;
		int     cur_y = center_v ? put_y + biggest_h / 4 : put_y;

		SDL_Rect dest { cur_x, cur_y, w, h };
		SDL_Rect src  { 0, 0, w, h };

		SDL_RenderCopy(sd->screen, p, &src, &dest);

		put_y  += h;
		work_h -= h;

		if (work_h <= 0)
			break;
	}

	lock.unlock();
}

scroller::scroller(SDL_Renderer * const renderer, const std::string & font_file, const int scroll_speed, const int font_height, const int r, const int g, const int b, const int max_width, base_text_formatter *const fmt, const int clear_after, const bool center_v) :
	container(renderer, font_file, font_height, max_width, fmt, clear_after), scroll_speed(scroll_speed),
	center_v(center_v)
{
	assert(renderer);

	col.r = r;
	col.g = g;
	col.b = b;

	th = new std::thread(std::ref(*this));
}

scroller::~scroller()
{
	th->join();
	delete th;
}

void scroller::put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center_h, const bool center_v)
{
	assert(0);
}

void scroller::put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h)
{
	lock.lock();

	if (!surfaces.empty()) {
		SDL_Rect dest { x * sd->xsteps + 1, y * sd->ysteps + 1, sd->xsteps * put_w, sd->ysteps * put_h };
		int cur_render_x = render_x;
		int pixels_to_do = sd->xsteps * put_w;

		do {
			for(auto & p : surfaces) {
				Uint32 format = 0;
				int access = 0;
				int w = 0;
				int h = 0;
				int rc = SDL_QueryTexture(p, &format, &access, &w, &h);
				assert(rc == 0);

				if (w <= cur_render_x) {
					cur_render_x -= w;
					continue;
				}

				SDL_Rect cur_src { cur_render_x, 0, w - cur_render_x, h };
				cur_render_x = 0;

				SDL_Rect dest_temp { 0 };

				if (center_v) {
					dest_temp = { dest.x, dest.y + sd->ysteps * put_h / 2 - h / 2, std::min(dest.w, cur_src.w), sd->ysteps * put_h };
				}
				else {
					dest_temp = { dest.x, dest.y, std::min(dest.w, cur_src.w), sd->ysteps * put_h };
				}

				SDL_RenderCopy(sd->screen, p, &cur_src, &dest_temp);
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

void scroller::operator()() {
	set_thread_name("scroller");

	while(!do_exit) {
		usleep(10000);

		lock.lock();

		if (total_w > 0) {

			render_x += scroll_speed;
			render_x %= total_w;
		}

		lock.unlock();
	}
}
