#include <cassert>
#include <cstring>
#include <mosquitto.h>
#include <csignal>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "container.h"
#include "proc.h"
#include "str.h"


class feed
{
protected:
	std::thread *th       { nullptr };
	container    *const c { nullptr };

public:
	feed(container *const c);
	virtual ~feed();

	virtual void operator()() = 0;
};

class static_feed : public feed
{
private:
	const std::vector<std::string> text;

public:
	static_feed(const std::string & text, container *const c);
	virtual ~static_feed();

	void operator()() override;
};

class mqtt_feed : public feed
{
private:
	struct mosquitto *mi { nullptr };

public:
	mqtt_feed(const std::string & host, const int port, const std::vector<std::string> & topics, container *const c);
	virtual ~mqtt_feed();

	void operator()() override;
};

class exec_feed : public feed
{
private:
	const std::string cmd;
	const int         interval_ms;

public:
	exec_feed(const std::string & cmd, const int interval_ms, container *const c);
	virtual ~exec_feed();

	void operator()() override;
};

class tail_feed : public feed
{
private:
	const std::string cmd;

public:
	tail_feed(const std::string & cmd, container *const c);
	virtual ~tail_feed();

	void operator()() override;
};
