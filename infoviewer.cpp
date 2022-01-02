#include <assert.h>
#include <atomic>
#include <jansson.h>
#include <libconfig.h++>
#include <math.h>
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

void error_exit(const bool se, const char *format, ...)
{
	int e = errno;
	va_list ap;

	va_start(ap, format);
	char *temp = NULL;
	if (vasprintf(&temp, format, ap) == -1)
		puts(format);  // last resort
	va_end(ap);

	fprintf(stderr, "%s\n", temp);

	if (se && e)
		fprintf(stderr, "errno: %d (%s)\n", e, strerror(e));

	free(temp);

	exit(EXIT_FAILURE);
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

double cfg_float(const libconfig::Setting & cfg, const char *const key, const char *descr, const bool optional, const double def=-1.0)
{
	double v = def;

	try {
		v = cfg.lookup(key);
	}
	catch(const libconfig::SettingNotFoundException &nfex) {
		if (!optional)
			error_exit(false, "\"%s\" not found (%s)", key, descr);
	}
	catch(const libconfig::SettingTypeException & ste) {
		error_exit(false, "Expected a float value for \"%s\" (%s) at line %d but got something else (did you forget to add \".0\"?)", key, descr, cfg.getSourceLine());
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

std::vector<std::string> split(std::string in, std::string splitter)
{
	std::vector<std::string> out;
	size_t splitter_size = splitter.size();

	for(;;)
	{
		size_t pos = in.find(splitter);
		if (pos == std::string::npos)
			break;

		std::string before = in.substr(0, pos);
		out.push_back(before);

		size_t bytes_left = in.size() - (pos + splitter_size);
		if (bytes_left == 0)
		{
			out.push_back("");
			return out;
		}

		in = in.substr(pos + splitter_size);
	}

	if (in.size() > 0)
		out.push_back(in);

	return out;
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

class text_formatter
{
public:
	text_formatter() {
	}

	virtual ~text_formatter() {
	}

	virtual std::string process(const std::string & in)
	{
		return in;
	}
};

class json_formatter : public text_formatter
{
private:
	const std::string format_string;

public:
	json_formatter(const std::string & format_string) : format_string(format_string) {
	}

	virtual ~json_formatter() {
	}

	std::string process(const std::string & in) override
	{
		json_error_t err { 0 };
		json_t *j = json_loads(in.c_str(), JSON_DECODE_ANY | JSON_ALLOW_NUL, &err);
		if (!j) {
			fprintf(stderr, "json decoding of \"%s\" failed: %s\n", in.c_str(), err.text);
			return "";
		}

		std::string out, cmd;
		bool get_cmd = false;

		for(size_t i=0; i<format_string.size(); i++) {
			if (get_cmd) {
				if (format_string.at(i) == '}') {
					if (cmd.substr(0, 8) == "jsonstr:") {
						json_t *j_obj = json_object_get(j, cmd.substr(8).c_str());
						if (j_obj)
							out += json_string_value(j_obj);
					}
					else if (cmd.substr(0, 8) == "jsonval:") {
						json_t *j_obj = json_object_get(j, cmd.substr(8).c_str());
						if (j_obj)
							out += myformat("%ld", json_integer_value(j_obj));
					}
					else {
						fprintf(stderr, "Format-string \"%s\" is not understood\n", cmd.c_str());
					}

					get_cmd = false;
					cmd.clear();
				}
				else {
					cmd += format_string.at(i);
				}
			}
			else if (format_string.at(i) == '{')
				get_cmd = true;
			else {
				out += format_string.at(i);
			}
		}

		json_decref(j);

		printf("%s\n", out.c_str());

		return out;
	}
};

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
	text_formatter *const fmt { nullptr };

public:
	container(const std::string & font_file, const int font_height, const int max_width, text_formatter *const fmt) : max_width(max_width), fmt(fmt)
	{
		font = load_font(font_file, font_height, true);
	}

	virtual ~container()
	{
		for(auto & s : surfaces)
			SDL_FreeSurface(s);
	}

	virtual std::pair<int, int> set_text(const std::vector<std::string> & in_)
	{
		std::vector<SDL_Surface *> temp_new;

		std::vector<std::string> in;

		// new text?
		std::string new_text;
		for(auto t : in_) {
			auto new_t = fmt ? fmt->process(t) : t;
			new_text += new_t;
			in.push_back(new_t);
		}

		if (new_text == text)
			// no; don't re-render
			return { total_w, h };

		// render new text
		int new_total_w = 0, new_h = 0;

		ttf_lock.lock();
		for(auto line : in) {
			int text_w = 0, dummy = 0;
			TTF_SizeUTF8(font, line.c_str(), &text_w, &dummy);

			int divider = ceil(double(text_w) / max_width);
			size_t n_chars = ceil(line.size() / double(divider));

			for(int i=0; i<divider; i++) {
				std::string part = line.substr(0, n_chars);
				if (part.empty())
					break;

				line.erase(0, n_chars);

				SDL_Surface *new_ = TTF_RenderUTF8_Blended(font, part.c_str(), col);
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

	virtual void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center) = 0;

	virtual void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h) = 0;
};

class text_box : public container
{
public:
	text_box(const std::string & font_file, const int font_height, const int r, const int g, const int b, const int max_width, text_formatter *const fmt) : container(font_file, font_height, max_width, fmt)
	{
		col.r = r;
		col.g = g;
		col.b = b;
	}

	virtual ~text_box()
	{
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h)
	{
		assert(0);
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
	int render_x { 0 }, scroll_speed { 1 };
	std::thread *th { nullptr };

public:
	scroller(const std::string & font_file, const int scroll_speed, const int font_height, const int r, const int g, const int b, const int max_width, text_formatter *const fmt) : container(font_file, font_height, max_width, fmt), scroll_speed(scroll_speed)
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
		return container::set_text(in);
	}

	void put_static(screen_descriptor_t *const sd, const int x, const int y, const int w, const int h, const bool center)
	{
		assert(0);
	}

	void put_scroller(screen_descriptor_t *const sd, const int x, const int y, const int put_w, const int put_h)
	{
		lock.lock();

		if (!surfaces.empty()) {
			SDL_Rect dest { x * sd->xsteps + 1, y * sd->ysteps + 1, sd->xsteps * put_w, sd->ysteps * put_h };
			int cur_render_x = render_x;
			int pixels_to_do = sd->xsteps * put_w;

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

				render_x += scroll_speed;
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

class feed
{
protected:
	std::thread *th { nullptr };
	container *const c { nullptr };

public:
	feed(container *const c) : c(c)
	{
	}

	virtual ~feed()
	{
	}

	virtual void operator()()
	{
	}
};

class static_feed : public feed
{
private:
	const std::vector<std::string> text;

public:
	static_feed(const std::string & text, container *const c) : feed(c), text(split(text, "\n"))
	{
		th = new std::thread(std::ref(*this));
	}

	virtual ~static_feed()
	{
		th->join();
		delete th;
	}

	void operator()() override
	{
		set_thread_name("static");

		for(;!do_exit;) {
			c->set_text(text);
			usleep(500000);
		}
	}
};

class mqtt_feed : public feed
{
private:
	struct mosquitto *mi { nullptr };
	static int nr;

public:
	mqtt_feed(const std::string & host, const int port, const std::vector<std::string> & topics, container *const c) : feed(c)
	{
		mi = mosquitto_new(myformat("infoviewer-%d", nr++).c_str(), true, c);

		mosquitto_connect(mi, host.c_str(), port, 30);

		mosquitto_message_v5_callback_set(mi, on_message);

		for(auto & topic : topics)
			mosquitto_subscribe(mi, 0, topic.c_str(), 0);

		th = new std::thread(std::ref(*this));
	}

	virtual ~mqtt_feed()
	{
		mosquitto_destroy(mi);
	}

	void operator()() override
	{
		set_thread_name("mqtt");

		for(;!do_exit;)
			mosquitto_loop(mi, 500, 1);
	}
};

class exec_feed : public feed
{
private:
	const std::string cmd;
	const int interval_ms;

public:
	exec_feed(const std::string & cmd, const int interval_ms, container *const c) : feed(c), cmd(cmd), interval_ms(interval_ms)
	{
		th = new std::thread(std::ref(*this));
	}

	virtual ~exec_feed()
	{
	}

	void operator()() override
	{
		set_thread_name("exec");

		for(;!do_exit;) {
			char buffer[65536] { 0 };

			FILE *fh = popen(cmd.c_str(), "r");
			if (fh) {
				if (fread(buffer, 1, sizeof buffer - 1, fh) == 0)
					buffer[0] = 0x00;

				pclose(fh);
				printf("%s\n", buffer);

				std::vector<std::string> parts = split(buffer, "\n");
				c->set_text(parts);
			}
			else {
				fprintf(stderr, "Cannot execute \"%s\"\n", cmd.c_str());
			}

			usleep(interval_ms * 1000);
		}
	}
};

class tail_feed : public feed
{
private:
	const std::string cmd;

public:
	tail_feed(const std::string & cmd, container *const c) : feed(c), cmd(cmd)
	{
		th = new std::thread(std::ref(*this));
	}

	virtual ~tail_feed()
	{
	}

	void operator()() override
	{
		set_thread_name("tail");

		FILE *fh = popen(cmd.c_str(), "r");
		if (!fh)
			fprintf(stderr, "Cannot execute \"%s\"\n", cmd.c_str());

		for(;!do_exit;) {
			char buffer[65536] { 0 };

			if (fgets(buffer, sizeof buffer, fh)) {
				printf("%s\n", buffer);

				std::vector<std::string> parts = split(buffer, "\n");
				c->set_text(parts);
			}
		}

		pclose(fh);
	}
};

int mqtt_feed::nr = 0; 

typedef enum { ct_static, ct_scroller } container_type_t;

typedef struct {
	container *c;
	container_type_t ct;
	int font_r, font_g, font_b;
	int bg_r, bg_g, bg_b;
	int x, y, w, h;
	bool border, center, bg_fill;
} container_t;

int main(int argc, char *argv[])
{
	signal(SIGTERM, sigh);

	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);

	TTF_Init();

	mosquitto_lib_init();

	libconfig::Config cfg;

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

	int n_columns = 80, n_rows = 25;
	bool grid = true;
	bool full_screen = true;

	int create_w = 800, create_h = 480;

	{
		const libconfig::Setting & global = root.lookup("global");

		n_columns = cfg_int(global, "n-columns", "number of columns", true, 80);
		n_rows = cfg_int(global, "n-rows", "number of rows", true, 25);

		grid = cfg_bool(global, "grid", "grid", true, false);
		full_screen = cfg_bool(global, "full-screen", "full screen", true, true);

		create_w = cfg_int(global, "window-w", "when not full screen, window width", true, 800);
		create_h = cfg_int(global, "window-h", "when not full screen, window height", true, 480);
	}

	const SDL_VideoInfo *svi = SDL_GetVideoInfo();
	SDL_Surface *screen = SDL_SetVideoMode(create_w, create_h, 32, (svi->hw_available ? SDL_HWSURFACE : SDL_SWSURFACE) | SDL_ASYNCBLIT | (full_screen ? SDL_FULLSCREEN : 0));
	const int w = screen->w;
	const int h = screen->h;
	const int xsteps = w / n_columns;
	const int ysteps = h / n_rows;
	screen_descriptor_t sd { screen, w, h, xsteps, ysteps };

	if (full_screen)
		SDL_ShowCursor(SDL_DISABLE);

	std::vector<container_t> containers;
	std::vector<feed *> feeds;

	const libconfig::Setting & instances = root["instances"];
	size_t n_instances = instances.getLength();

	for(size_t i=0; i<n_instances; i++) {
		const libconfig::Setting & instance = instances[i];

		std::string formatter_type = cfg_str(instance, "formatter", "json or as-is", false, "as-is");
		text_formatter *tf { nullptr };

		if (formatter_type == "json") {
			std::string format_string = cfg_str(instance, "format-string", "json", false, "");

			tf = new json_formatter(format_string);
		}
		else if (formatter_type == "as-is") {
			tf = new text_formatter();
		}
		else {
			error_exit(false, "\"format-string %s\" unknown", formatter_type.c_str());
		}

		container *c { nullptr };

		std::string font = cfg_str(instance, "font", "path to font", false, "/usr/share/vlc/skins2/fonts/FreeSans.ttf");
		int font_height = cfg_int(instance, "font-height", "font height", false, 5);

		int max_width = cfg_int(instance, "max-width", "max text width", false, 5);

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

		bool bg_fill = cfg_bool(instance, "bg-fill", "fill background", true, true);

		int x = cfg_int(instance, "x", "x position", false, 0);
		int y = cfg_int(instance, "y", "y position", false, 0);
		int w = cfg_int(instance, "w", "w position", false, 1);
		int h = cfg_int(instance, "h", "h position", false, 1);

		bool center = cfg_bool(instance, "center", "center", true, true);

		container_type_t ct;

		std::string type = cfg_str(instance, "type", "scroller or static", false, "static");
		if (type == "static") {
			ct = ct_static;
			c = new text_box(font, ysteps * font_height, fg_r, fg_g, fg_b, max_width * xsteps, tf);
		}
		else if (type == "scroller") {
			ct = ct_scroller;
			int scroll_speed = cfg_int(instance, "scroll-speed", "pixel count", true, 1);
			c = new scroller(font, scroll_speed, ysteps * font_height, fg_r, fg_g, fg_b, max_width * xsteps, tf);
		}
		else {
			error_exit(false, "\"type %s\" unknown", type.c_str());
		}

		container_t entry { 0 };
		entry.c = c;
		entry.ct = ct;
		entry.font_r = fg_r;
		entry.font_g = fg_g;
		entry.font_b = fg_b;
		entry.bg_r = bg_r;
		entry.bg_g = bg_g;
		entry.bg_b = bg_b;
		entry.bg_fill = bg_fill;
		entry.x = x;
		entry.y = y;
		entry.w = w;
		entry.h = h;
		entry.border = cfg_bool(instance, "border", "border", false, true);
		entry.center = center;
		
		containers.push_back(entry);

		const libconfig::Setting & s_feed = instance["feed"];
		std::string feed_type = cfg_str(s_feed, "feed-type", "mqtt, exec, tail or static", false, "mqtt");

		feed *f { nullptr };

		if (feed_type == "mqtt") {
			std::string host = cfg_str(s_feed, "host", "mqtt host", false, "127.0.0.1");
			int port = cfg_int(s_feed, "port", "mqtt port", true, 1883);

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

	for(;!do_exit;) {
		if (grid) {
			for(int cy=0; cy<n_rows; cy++)
				lineRGBA(screen, 0, cy * ysteps, w, cy * ysteps, 255, 255, 255, 255);

			for(int cx=0; cx<n_columns; cx++)
				lineRGBA(screen, cx * xsteps, 0, cx * xsteps, h, 255, 255, 255, 255);
		}

		for(auto & c : containers) {
			draw_box(&sd, c.x, c.y, c.w, c.h, c.border, c.bg_r, c.bg_g, c.bg_b);

			if (c.ct == ct_static)		
				c.c->put_static(&sd, c.x, c.y, c.w, c.h, c.center);
			else if (c.ct == ct_scroller)
				c.c->put_scroller(&sd, c.x, c.y, c.w, c.h);
			else
				error_exit(false, "Internal error: unknown container type %d", c.ct);
		}

		SDL_UpdateRect(screen, 0, 0, w, h);

		usleep(10000);

		SDL_Event event { 0 };
		if (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT) {
				do_exit = true;
				break;
			}
		}
	}

	mosquitto_lib_cleanup();

	return 0;
}
