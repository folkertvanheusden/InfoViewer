#include <atomic>
#include <cassert>
#include <cstring>
#include <mosquitto.h>
#include <csignal>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "container.h"
#include "error.h"
#include "feeds.h"
#include "proc.h"
#include "str.h"


extern std::atomic_bool do_exit;

extern void set_thread_name(const std::string & name);

void on_message(struct mosquitto *, void *arg, const struct mosquitto_message *msg, const mosquitto_property *)
{
	container *c = (container *)arg;

	std::string new_text((const char *)msg->payload, msg->payloadlen);

	auto result = c->set_text({ new_text });
	printf("on_message: %s (%dx%d)\n", new_text.c_str(), result.first, result.second);
}

feed::feed(container *const c) : c(c)
{
}

feed::~feed()
{
}

static_feed::static_feed(const std::string & text, container *const c) : feed(c), text(split(text, "\n"))
{
	assert(c);

	th = new std::thread(std::ref(*this));
}

static_feed::~static_feed()
{
	th->join();
	delete th;
}

void static_feed::operator()()
{
	set_thread_name("static");

	while(!do_exit) {
		c->set_text(text);
		usleep(500000);
	}
}

mqtt_feed::mqtt_feed(const std::string & host, const int port, const std::vector<std::string> & topics, container *const c) : feed(c)
{
	mi = mosquitto_new(nullptr, true, c);
	if (!mi)
		error_exit(false, "Cannot crate mosquitto instance");

	int err = 0;
	if ((err = mosquitto_connect(mi, host.c_str(), port, 30)) != MOSQ_ERR_SUCCESS)
		fprintf(stderr, "mqtt failed to connect (%s)\n", mosquitto_strerror(err));

	mosquitto_message_v5_callback_set(mi, on_message);

	for(auto & topic : topics)
		mosquitto_subscribe(mi, 0, topic.c_str(), 0);

	th = new std::thread(std::ref(*this));
}

mqtt_feed::~mqtt_feed()
{
	mosquitto_destroy(mi);
}

void mqtt_feed::operator()()
{
	set_thread_name("mqtt");

	while(!do_exit) {
		int err = 0;

		if ((err = mosquitto_loop(mi, 500, 1)) != MOSQ_ERR_SUCCESS) {
			fprintf(stderr, "mqtt error (%s), reconnecting\n", mosquitto_strerror(err));

			sleep(1);

			if ((err = mosquitto_reconnect(mi)) != MOSQ_ERR_SUCCESS)
				fprintf(stderr, "mqtt reconnect failed (%s)\n", mosquitto_strerror(err));
		}
	}
}

exec_feed::exec_feed(const std::string & cmd, const int interval_ms, container *const c) : feed(c), cmd(cmd), interval_ms(interval_ms)
{
	th = new std::thread(std::ref(*this));
}

exec_feed::~exec_feed()
{
}

void exec_feed::operator()()
{
	set_thread_name("exec");

	while(!do_exit) {
		auto rc = exec_with_pipe(cmd, ".", 80, 25, -1, true, true);

		char buffer[65536] { 0 };

		int n_chars = read(std::get<1>(rc), buffer, sizeof(buffer) - 1);

		close(std::get<1>(rc));

		kill(SIGTERM, std::get<0>(rc));

		if (n_chars <= 0)
			break;

		buffer[n_chars] = 0x00;

		for(;;) {
			char *p = strchr(buffer, '\r');
			if (!p)
				break;

			size_t to_move = n_chars - (p + 1 - buffer);

			if (to_move > 0) {
				memmove(p, p + 1, to_move);
				buffer[n_chars--] = 0x00;
			}
		}

		std::vector<std::string> parts = split(buffer, "\n");
		c->set_text(parts);

		usleep(interval_ms * 1000);
	}
}

tail_feed::tail_feed(const std::string & cmd, container *const c) : feed(c), cmd(cmd)
{
	th = new std::thread(std::ref(*this));
}

tail_feed::~tail_feed()
{
}

void tail_feed::operator()()
{
	set_thread_name("tail");

	auto rc = exec_with_pipe(cmd, ".", 80, 25, 1, true, true);

	std::string buffer;

	for(;;) {
		char chr = 0;

		int read_rc = read(std::get<1>(rc), &chr, 1);
		if (read_rc <= 0)
			break;

		if (chr == 13)
			continue;

		if (chr == 10) {
			c->set_text({ buffer });

			buffer.clear();
		}
		else {
			buffer += chr;
		}
	}

	close(std::get<1>(rc));

	kill(SIGTERM, std::get<0>(rc));
}
