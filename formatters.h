#include <optional>
#include <string>

class base_text_formatter
{
public:
	base_text_formatter();
	virtual ~base_text_formatter();

	virtual std::string process(const std::string & in) = 0;
};

class text_formatter : public base_text_formatter
{
private:
	const std::optional<std::string> format;
	size_t len { 0 };

	std::string do_cmd(const std::string & in, const std::string & cmd);

public:
	text_formatter(const std::optional<std::string> & format);
	virtual ~text_formatter();

	std::string process(const std::string & in) override;
};

class json_formatter : public base_text_formatter
{
private:
	const std::string format_string;

public:
	json_formatter(const std::string & format_string);
	virtual ~json_formatter();

	std::string process(const std::string & in) override;
};
