#pragma once
#include <mutex>
#include <string>
#include <thread>
#include <time.h>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "formatters.h"


typedef struct
{
	SDL_Renderer *screen;
	int scr_w, scr_h;
	int xsteps, ysteps;
} screen_descriptor_t;

class container
{
private:
	TTF_Font *font { nullptr };

protected:
	SDL_Renderer   *renderer       { nullptr    };
	const int       max_width      { 0          };
	std::mutex      lock;
	std::vector<SDL_Texture *> surfaces;
	std::string     text;
	int             total_w        { 0          };
	int             h              { 0          };
	SDL_Color       col            { 0, 0, 0, 0 };
	base_text_formatter *const fmt { nullptr    };
	const int       clear_after    { -1         };
	time_t          most_recent_update { 0      };
	std::thread    *th             { nullptr    };

public:
	container(SDL_Renderer *const renderer, const std::string & font_file, const int font_height, const int max_width, base_text_formatter *const fmt, const int clear_after);
	virtual ~container();

	void operator()();

	virtual std::pair<int, int> set_text  (const std::vector<std::string> & in_);
	virtual std::pair<int, int> set_pixels(const uint8_t *const rgb_pixels, const int width, const int height);

	virtual void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center_h, const bool center_v) = 0;

	virtual void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h) = 0;
};

class text_box : public container
{
public:
	text_box(SDL_Renderer * const renderer, const std::string & font_file, const int font_height, const int r, const int g, const int b, const int max_width, base_text_formatter *const fmt, const int clear_after);
	virtual ~text_box();

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h);
	void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center_h, const bool center_v);
};

class scroller : public container
{
private:
	int  render_x     { 0     };
	int  scroll_speed { 1     };
	bool center_v     { false };

	std::thread *th { nullptr };

public:
	scroller(SDL_Renderer * const renderer, const std::string & font_file, const int scroll_speed, const int font_height, const int r, const int g, const int b, const int max_width, base_text_formatter *const fmt, const int clear_after, const bool center_v);
	virtual ~scroller();

	void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center_h, const bool center_v);
	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h);

	void operator()();
};
