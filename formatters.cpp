#include <jansson.h>
#include <optional>
#include <regex>
#include <string>

#include "error.h"
#include "formatters.h"
#include "str.h"


base_text_formatter::base_text_formatter() {
}

base_text_formatter::~base_text_formatter() {
}

text_formatter::text_formatter(const std::optional<std::string> & format) : format(format)
{
	if (format.has_value())
		len = format.value().size();
}

text_formatter::~text_formatter() {
}

std::string text_formatter::do_cmd(const std::string & in, const std::string & cmd) {
	std::string out;

	auto cmd_parts = split(cmd, ":");

	// field:in_seperator:out_seperator:fieldnr,fieldnr,fieldnr,...
	if (cmd_parts.at(0) == "field") {
		if (cmd_parts.size() != 4)
			error_exit(false, "\"%s\": fields missing", cmd.c_str());

		auto in_parts    = split(in, cmd_parts.at(1));

		auto field_parts = split(cmd_parts.at(3), ",");

		bool first       = true;

		for(auto & field : field_parts) {
			if (first)
				first = false;
			else
				out += cmd_parts.at(2);

			size_t field_nr = atoi(field.c_str());

			if (field_nr < in_parts.size())
				out += in_parts[field_nr];
		}
	}
	// regex:seperator:re...
	else if (cmd_parts.at(0) == "regex") {
		if (cmd_parts.size() != 3)
			error_exit(false, "\"%s\": fields missing", cmd.c_str());

		std::regex  regexp(cmd_parts.at(2));
		std::smatch m;

		std::regex_search(in, m, regexp);

		bool first = true;

		for(auto & field : m) {
			if (first)
				first = false;
			else {
				out += cmd_parts.at(1);

				out += field;
			}
		}
	}
	else {
		fprintf(stderr, "Escape %s not known\n", cmd_parts[0].c_str());
	}

	return out;
}

std::string text_formatter::process(const std::string & in)
{
	if (format.has_value() == false)
		return in;

	std::string out;
	std::string cmd;

	size_t pos = 0;

	bool processing = false;

	while(pos < len) {
		if (processing) {
			if (format.value().at(pos) == '$') {
				out += do_cmd(in, cmd);

				processing = false;
			}
			else {
				cmd += format.value().at(pos);
			}
		}
		else if (format.value().at(pos) == '$') {
			processing = true;

			cmd.clear();
		}
		else {
			out += format.value().at(pos);
		}

		pos++;
	}

	if (processing)
		out += do_cmd(in, cmd);

	return out;
}

json_formatter::json_formatter(const std::string & format_string) : format_string(format_string) {
}

json_formatter::~json_formatter() {
}

std::string json_formatter::process(const std::string & in)
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
					if (j_obj && json_is_string(j_obj))
						out += json_string_value(j_obj);
					else
						out += "?";
				}
				else if (cmd.substr(0, 8) == "jsonval:") {
					json_t *j_obj = json_object_get(j, cmd.substr(8).c_str());
					if (j_obj && json_is_integer(j_obj))
						out += myformat("%ld", json_integer_value(j_obj));
					else
						out += "?";
				}
				else if (cmd.substr(0, 9) == "jsondval:") {
					std::vector<std::string> parts = split(cmd, ":");

					int digits = atoi(parts.at(1).c_str());
					std::string format = myformat("%%.%df", digits);

					json_t *j_obj = json_object_get(j, parts.at(2).c_str());
					if (j_obj && json_is_real(j_obj))
						out += myformat(format.c_str(), json_real_value(j_obj));
					else
						out += "?";
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
