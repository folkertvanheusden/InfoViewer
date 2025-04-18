#include <assert.h>
#include <atomic>
#include <libconfig.h++>
#include <math.h>
#include <mosquitto.h>
#include <mutex>
#include <optional>
#include <signal.h>
#include <stdarg.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "error.h"
#include "feeds.h"
#include "formatters.h"
#include "str.h"


std::atomic_bool do_exit { false };

void sigh(int s)
{
	do_exit = true;
}

std::string cfg_str(const libconfig::Setting & cfg, const std::string & key, const char *descr, const bool optional, const std::string & def)
{
	std::string v = def;

	try {
		v = (const char *)cfg.lookup(key.c_str());
	}
	catch(const libconfig::SettingNotFoundException &nfex) {
		if (!optional)
			error_exit(false, "\"%s\" not found (%s)", key.c_str(), descr);
	}
	catch(const libconfig::SettingTypeException & ste) {
		error_exit(false, "Expected a string value for \"%s\" (%s) at line %d but got something else (%s)", key.c_str(), descr, cfg.getSourceLine(), ste.what());
	}

	return v;
}

double cfg_float(const libconfig::Setting & cfg, const std::string & key, const char *descr, const bool optional, const double def=-1.0)
{
	double v = def;

	try {
		cfg.lookupValue(key.c_str(), v);
	}
	catch(const libconfig::SettingNotFoundException &nfex) {
		if (!optional)
			error_exit(false, "\"%s\" not found (%s)", key.c_str(), descr);
	}
	catch(const libconfig::SettingTypeException & ste) {
		error_exit(false, "Expected a float value for \"%s\" (%s) at line %d but got something else (did you forget to add \".0\"?)", key.c_str(), descr, cfg.getSourceLine());
	}

	return v;
}

int cfg_int(const libconfig::Setting & cfg, const std::string & key, const char *descr, const bool optional, const int def=-1)
{
	int v = def;

	try {
		v = cfg.lookup(key.c_str());
	}
	catch(const libconfig::SettingNotFoundException &nfex) {
		if (!optional)
			error_exit(false, "\"%s\" not found (%s)", key.c_str(), descr);
	}
	catch(const libconfig::SettingTypeException & ste) {
		error_exit(false, "Expected an int value for \"%s\" (%s) at line %d but got something else", key.c_str(), descr, cfg.getSourceLine());
	}

	return v;
}

int cfg_bool(const libconfig::Setting & cfg, const char *const key, const char *descr, const bool optional, const bool def=false)
{
	bool v = def;

	try {
		v = cfg.lookup(key);
	}
	catch(const libconfig::SettingNotFoundException &nfex) {
		if (!optional)
			error_exit(false, "\"%s\" not found (%s)", key, descr);
	}
	catch(const libconfig::SettingTypeException & ste) {
		error_exit(false, "Expected a boolean value for \"%s\" (%s) but got something else", key, descr);
	}

	return v;
}

void set_thread_name(const std::string & name)
{
	std::string full_name = "IV:" + name;

	if (full_name.length() > 15)
		full_name = full_name.substr(0, 15);

	pthread_setname_np(pthread_self(), full_name.c_str());
}

void draw_box(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool border, const int r, const int g, const int b, const int b_r, const int b_g, const int b_b)
{
	assert(sd->screen);

	int x1 = x * sd->xsteps;
	int y1 = y * sd->ysteps;
	int x2 = x1 + sd->xsteps * w - 1;
	int y2 = y1 + sd->ysteps * h - 1;

	boxRGBA(sd->screen, x1, y1, x2, y2, r, g, b, 255);

	if (border)
		rectangleRGBA(sd->screen, x1, y1, x2, y2, b_r, b_g, b_b, 191);
}

std::mutex ttf_lock;
typedef enum { ct_static, ct_scroller } container_type_t;

typedef struct {
	container *c;
	container_type_t ct;
	int font_r, font_g, font_b;
	int bg_r, bg_g, bg_b;
	int b_r, b_g, b_b;
	int x, y, w, h;
	bool border, center_h, center_v, bg_fill;
} container_t;

int main(int argc, char *argv[])
{
	signal(SIGTERM, sigh);

	if (argc != 2) {
		fprintf(stderr, "Configuration file parameter missing\n");
		return 1;
	}

	if (SDL_Init(SDL_INIT_VIDEO) == -1) {
                fprintf(stderr, "Failed to initialize SDL video subsystem\n");
                return 1;
	}

	atexit(SDL_Quit);

	TTF_Init();

	mosquitto_lib_init();

	libconfig::Config cfg;
#if (LIBCONFIGXX_VER_MAJOR >= 1 && LIBCONFIGXX_VER_MINOR >= 7)
	cfg.setOptions(libconfig::Config::Option::OptionAutoConvert);
#else
	cfg.setOptions(libconfig::Setting::Option::OptionAutoConvert);
#endif

        try {
                cfg.readFile(argv[1]);
        }
        catch(const libconfig::FileIOException &fioex) {
                fprintf(stderr, "I/O error while reading configuration file %s\n", argv[1]);
                return 1;
        }
        catch(const libconfig::ParseException &pex) {
                fprintf(stderr, "Configuration file %s parse error at line %d: %s\n", pex.getFile(), pex.getLine(), pex.getError());
                return 1;
        }

	const libconfig::Setting & root = cfg.getRoot();

	int  n_columns   = 80;
	int  n_rows      = 25;
	bool grid        = true;
	bool full_screen = true;

	int  create_w    = 800;
	int  create_h    = 480;

	int  display_nr  = 0;

	try {
		const libconfig::Setting & global = root.lookup("global");

		n_columns = cfg_int(global, "n-columns", "number of columns", true, 80);
		n_rows = cfg_int(global, "n-rows", "number of rows", true, 25);

		grid = cfg_bool(global, "grid", "grid", true, false);
		full_screen = cfg_bool(global, "full-screen", "full screen", true, true);

		create_w = cfg_int(global, "window-w", "when not full screen, window width", true, 800);
		create_h = cfg_int(global, "window-h", "when not full screen, window height", true, 480);

		display_nr = cfg_int(global, "display-nr", "with multiple monitors, use this monitor", true, 1);
	}
	catch(libconfig::SettingNotFoundException & e) {
                fprintf(stderr, "Configuration group \"global\" not found!\n");
                return 1;
	}

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

	SDL_Window *win = SDL_CreateWindow("InfoViewer",
                          SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_nr),
                          SDL_WINDOWPOS_UNDEFINED_DISPLAY(display_nr),
                          create_w, create_h,
                          (full_screen ? SDL_WINDOW_FULLSCREEN : 0) | SDL_WINDOW_OPENGL);
	assert(win);

	SDL_Renderer *screen = SDL_CreateRenderer(win, -1, 0);
	assert(screen);

	int w = 0;
	int h = 0;
	SDL_GetWindowSize(win, &w, &h);
	printf("%dx%d\n", w, h);

	const int xsteps = w / n_columns;
	const int ysteps = h / n_rows;
	screen_descriptor_t sd { screen, w, h, xsteps, ysteps };

	if (full_screen)
		SDL_ShowCursor(SDL_DISABLE);

	std::vector<container_t> containers;
	std::vector<feed *>      feeds;

	const libconfig::Setting & instances   = root["instances"];
	size_t                     n_instances = instances.getLength();

	for(size_t i=0; i<n_instances; i++) {
		const libconfig::Setting & instance = instances[i];

		std::string formatter_type = cfg_str(instance, "formatter", "json, text or as-is", false, "as-is");
		base_text_formatter *tf { nullptr };

		std::string format_string;

		if (formatter_type == "json") {
			std::string format_string = cfg_str(instance, "format-string", "json", false, "");

			tf = new json_formatter(format_string);
		}
		else if (formatter_type == "as-is") {
			tf = new text_formatter({ });
		}
		else if (formatter_type == "text") {
			std::string format_string = cfg_str(instance, "format-string", "text", false, "");

			tf = new text_formatter(format_string);
		}
		else {
			error_exit(false, "\"format-string %s\" unknown", formatter_type.c_str());
		}

		container *c { nullptr };

		std::string font = cfg_str(instance, "font", "path to font", false, "/usr/share/vlc/skins2/fonts/FreeSans.ttf");
		double font_height = cfg_float(instance, "font-height", "font height", false, 5.0);

		int max_width = cfg_int(instance, "max-width", "max text width", false, 5);

		int clear_after = cfg_int(instance, "clear-after", "clear text after (in seconds)", true, -1);

		std::string color = cfg_str(instance, "fg-color", "r,g,b triple", true, "255,0,0");
		std::vector<std::string> color_str = split(color, ",");
		int fg_r = atoi(color_str.at(0).c_str());
		int fg_g = atoi(color_str.at(1).c_str());
		int fg_b = atoi(color_str.at(2).c_str());

		std::string bg_color = cfg_str(instance, "bg-color", "r,g,b triple", true, "255,0,0");
		std::vector<std::string> bg_color_str = split(bg_color, ",");
		int bg_r = atoi(bg_color_str.at(0).c_str());
		int bg_g = atoi(bg_color_str.at(1).c_str());
		int bg_b = atoi(bg_color_str.at(2).c_str());

		std::string b_color = cfg_str(instance, "b-color", "r,g,b triple", true, "255,0,0");
		std::vector<std::string> b_color_str = split(b_color, ",");
		int b_r = atoi(b_color_str.at(0).c_str());
		int b_g = atoi(b_color_str.at(1).c_str());
		int b_b = atoi(b_color_str.at(2).c_str());

		bool bg_fill = cfg_bool(instance, "bg-fill", "fill background", true, true);

		int x = cfg_int(instance, "x", "x position", false, 0);
		int y = cfg_int(instance, "y", "y position", false, 0);
		int w = cfg_int(instance, "w", "w position", false, 1);
		int h = cfg_int(instance, "h", "h position", false, 1);

		bool center_h = cfg_bool(instance, "center-horizontal", "center horizontal", true, true);
		bool center_v = cfg_bool(instance, "center-vertical",   "center vertical",   true, true);

		container_type_t ct;

		std::string type = cfg_str(instance, "type", "scroller or static", false, "static");
		if (type == "static") {
			ct = ct_static;
			c = new text_box(screen, font, ysteps * font_height, fg_r, fg_g, fg_b, max_width * xsteps, tf, clear_after);
		}
		else if (type == "scroller") {
			ct = ct_scroller;
			int scroll_speed = cfg_int(instance, "scroll-speed", "pixel count", true, 1);
			c = new scroller(screen, font, scroll_speed, ysteps * font_height, fg_r, fg_g, fg_b, max_width * xsteps, tf, clear_after, center_v);
		}
		else {
			error_exit(false, "\"type %s\" unknown", type.c_str());
		}

		container_t entry { 0 };
		entry.c        = c;
		entry.ct       = ct;
		entry.font_r   = fg_r;
		entry.font_g   = fg_g;
		entry.font_b   = fg_b;
		entry.bg_r     = bg_r;
		entry.bg_g     = bg_g;
		entry.bg_b     = bg_b;
		entry.b_r      = b_r;
		entry.b_g      = b_g;
		entry.b_b      = b_b;
		entry.bg_fill  = bg_fill;
		entry.x        = x;
		entry.y        = y;
		entry.w        = w;
		entry.h        = h;
		entry.border   = cfg_bool(instance, "border", "border", false, true);
		entry.center_h = center_h;
		entry.center_v = center_v;

		containers.push_back(entry);

		const libconfig::Setting & s_feed = instance["feed"];
		std::string feed_type = cfg_str(s_feed, "feed-type", "mqtt, exec, tail or static", false, "mqtt");

		feed *f { nullptr };

		if (feed_type == "mqtt") {
			std::string host = cfg_str(s_feed, "host", "mqtt host", false, "127.0.0.1");
			int         port = cfg_int(s_feed, "port", "mqtt port", true, 1883);

			const libconfig::Setting & s_topics = s_feed["topics"];
			size_t n_topics = s_topics.getLength();

			std::vector<std::string> topics;

			for(size_t i=0; i<n_topics; i++) {
				const libconfig::Setting & s_topic = s_topics[i];

				std::string topic = cfg_str(s_topic, "topic", "mqtt topic", false, "#");

				topics.push_back(topic);
			}

			f = new mqtt_feed(host, port, topics, c);
		}
		else if (feed_type == "exec") {
			std::string cmd = cfg_str(s_feed, "cmd", "command to invoke", false, "date");
			int interval = cfg_int(s_feed, "interval", "exec interval (in millisecons)", true, 1000);

			f = new exec_feed(cmd, interval, c);
		}
		else if (feed_type == "tail") {
			std::string cmd = cfg_str(s_feed, "cmd", "command to \"tail\"", false, "tail -f /var/log/messages");

			f = new tail_feed(cmd, c);
		}
		else if (feed_type == "static") {
			std::string text = cfg_str(s_feed, "text", "text to display", false, "my text");

			f = new static_feed(text, c);
		}
		else {
			error_exit(false, "\"feed-type %s\" unknown", feed_type.c_str());
		}

		feeds.push_back(f);
	}

	while(!do_exit) {
		if (grid) {
			for(int cy=0; cy<n_rows; cy++)
				lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);

			for(int cx=0; cx<n_columns; cx++)
				lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
		}

		for(auto & c : containers) {
			if (c.bg_fill)
				draw_box(&sd, c.x, c.y, c.w, c.h, c.border, c.bg_r, c.bg_g, c.bg_b, c.b_r, c.b_g, c.b_b);

			if (c.ct == ct_static)		
				c.c->put_static(&sd, c.x, c.y, c.w, c.h, c.center_h, c.center_v);
			else if (c.ct == ct_scroller)
				c.c->put_scroller(&sd, c.x, c.y, c.w, c.h);
			else
				error_exit(false, "Internal error: unknown container type %d", c.ct);
		}

		SDL_RenderPresent(screen);

		SDL_Delay(10);

		SDL_Event event { 0 };
		if (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) {
				do_exit = true;
				break;
			}

			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q) {
				do_exit = true;
				break;
			}

			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
				SDL_SetRenderDrawColor(screen, 0, 0, 0, 255);
				SDL_RenderClear(screen);
			}
		}
	}

	mosquitto_lib_cleanup();

	return 0;
}
