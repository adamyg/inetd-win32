/* -*- mode: c; indent-width: 8; -*- */
/*
 * Configuration
 * windows inetd service -- xinetd style.
 *
 * Copyright (c) 2022, Adam Young.
 * All rights reserved.
 *
 * The applications are free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * Redistributions of source code must retain the above copyright
 * notice, and must be distributed with the license document above.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, and must include the license document above in
 * the documentation and/or other materials provided with the
 * distribution.
 *
 * This project is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * license for more details.
 * ==end==
 */

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include <dirent.h>


/*
 *  defaults
 *  {
 *      <attribute> = <value> <value> ...
 *          :
 *  }
 *
 *  service <service_name>
 *  {
 *      <attribute> <assign_op> <value> <value> ...
 *          :
 *  }
 *
 */

struct configparams;

namespace xinetd {

class Attribute {
    public:
	template <typename Split>
	Attribute(const std::string &key, char op, const std::string &val, Split split)
		: key(key), value(val), values(split(val)), op(op)
	{
	}

	const std::string key;
	const std::vector<std::string> values;
	const std::string value;
	const char op; // '=', '+' or '-'.
};


class Collection;

class Attributes {
    public:
	using Container = std::vector<Attribute *>;
	using const_iterator = Container::const_iterator;

    public:
	Attributes(const std::string &name) : name_(name)
	{
	}

	~Attributes()
	{
		for (auto attribute : values_)
			delete attribute;
	}

	const std::string &name() const
	{
		return name_;
	}

	void push_back(Attribute *attr)
	{
		values_.push_back(attr);
	}

	const_iterator begin() const
	{
		return values_.begin();
	}

	const_iterator end() const
	{
		return values_.end();
	}

	const_iterator find(const char *key) const
	{
		return std::find_if(begin(), end(), [&](auto &attribute) {
					return (0 == strcmp(key, attribute->key.c_str()));
				});
	}

	const_iterator find(const char *key, const char *key_end) const
	{
		if (!key && key >= key_end)
			return const_iterator();

		const size_t key_len = key_end - key;
		return std::find_if(begin(), end(), [&](auto &attribute) {
					return (attribute->key.length() == key_len &&
						0 == strncmp(attribute->key.c_str(), key, key_len));
				});
	}

	const_iterator find(const std::string &key) const
	{
		if (key.empty())
			return const_iterator();

		return std::find_if(begin(), end(), [&](auto &attribute) {
					return (0 == strcmp(key.c_str(), attribute->key.c_str()));
				});
	}

	const_iterator find_next(const char *key, const_iterator it) const
	{
		const_iterator eit(end());
		if (it == eit)
			return const_iterator();

		return std::find_if(++it, eit, [&](auto &attribute) {
					return (0 == strcmp(key, attribute->key.c_str()));
				});
	}

    private:
	friend class Collection;
	const std::string name_;
	Container values_;
};


class Exception : public std::runtime_error {
public:
	enum Type : int {
		SYNTAX_ERROR = -1,
		INVALID_FILE = 1,
		INVALID_DIRECTORY,
		INVALID_SECTION,
		INVALID_ATTRIBUTE,
		INVALID_VARIABLE,
		INVALID_PARAMETER
	};

public:
	Exception(Type error_code, const std::string &message)
		: std::runtime_error(message), error_code(error_code)
	{
		assert(error_code);
	}

	static inline Exception
	File(const std::string &message) {
		return Exception(Exception::INVALID_FILE, message);
	}

	static inline Exception
	Directory(const std::string &message) {
		return Exception(Exception::INVALID_DIRECTORY, message);
	}

	static inline Exception
	Section(const std::string &message) {
		return Exception(Exception::INVALID_SECTION, message);
	}

	static inline Exception
	Attribute(const std::string &message) {
		return Exception(Exception::INVALID_ATTRIBUTE, message);
	}

	static inline Exception
	Variable(const std::string &message) {
		return Exception(Exception::INVALID_VARIABLE, message);
	}

	Type error_code;
};


class Split {
    public:
	enum { SPLIT_ESCAPES = 1, SPLIT_EXPAND = 2 };

    public:
	Split(class Attributes *defaults) : defaults_(defaults)
	{
	}

    public:
	std::vector<std::string>
	operator()(const std::string &value, unsigned options = SPLIT_ESCAPES|SPLIT_EXPAND)
	{
		std::vector<std::string> values;
		if (char *t_value = ::_strdup(value.c_str())) {
			emplace_split(values, t_value, options);
			::free(t_value);
		}
		return values;
	}

	void
	emplace_split(std::vector<std::string> &values, char *cmd, unsigned options)
	{
		const bool escapes = (SPLIT_ESCAPES & options) ? true : false;
		char *start, *end;

		for (;;) {
			// skip over blanks
			while (' ' == *cmd || '\t' == *cmd || '\n' == *cmd) {
				++cmd;		/* white-space */
			}

			if (!*cmd) break;

			// next argument
			if ('\"' == *cmd || '\'' == *cmd) {
						/* quoted argument */
				const char quote = *cmd++;

				start = end = cmd;
				for (;;) {
					const char ch = *cmd;
					if (ch == quote) {
						break;
					}
					if (!ch || '\n' == ch) {
						throw Exception::Section("unmatched quotes");
					}
					if (escapes && ch == '\\') {
						if ('\"' == cmd[1] || '\'' == cmd[1] || '\\' == cmd[1]) {
							++cmd;
						}
					}
					*end++ = *cmd++;
				}

			} else {
				start = end = cmd;
				for (;;) {
					const char ch = *cmd;
					if (!ch || ch == '\n' || ch == ' ' || ch == '\t') {
						break;
					}
					if (escapes && ch == '\\') {
						if ('\"' == cmd[1] || '\'' == cmd[1] || '\\' == cmd[1]) {
							++cmd;
						}
					}
					*end++ = *cmd++;
				}
			}

			// result
			if (defaults_ && (SPLIT_EXPAND & options)) {
				expand(start, end, 0, values);
			} else {
				values.push_back(std::string(start, end - start));
			}

			if (!*cmd) break;
			*end = 0;
			++cmd;
		}
	}

    private:
	void
	expand(const char *value, const char *end, unsigned level, std::vector<std::string> &results)
	{
		if ('$' != value[0] || '(' != value[1]) {
			results.emplace_back(value, end);
			return;
		}

		value += 2;
		if (')' != end[-1]) {
			throw Exception::Variable("variable syntax");
		}

		if (! var_is_valid(value, end - 1)) {
			throw Exception::Variable("invalid variable name");
		}

		if (level > 4) {
			throw Exception::Variable("excessive variable nesting of 4");
		}

		auto it = defaults_->find(value, --end);
		if (it != defaults_->end()) {
			const auto &values = (*it)->values;
			for (const auto &value : values) {
				expand(value.data(), value.data() + value.length(), level + 1, results);
			}
			return;
		}

		throw Exception::Variable("unknown variable <" + std::string(value, end) + ">");
	}

    private:
	static bool
	var_is_valid(const char *begin, const char *end)
	{
		return std::find_if(begin, end, [](char c) {
					return !(std::isalnum(c) || c == '_');
				}) == end;
	}

    private:
	const Attributes *defaults_;
};


class Collection {
    private:
    public:
	using Container = std::vector<std::shared_ptr<Attributes>>;
	using const_iterator = Container::const_iterator;

    public:
	Collection() : line_number_(0)
	{
	}

	Collection(std::istream &stream, const char *filename) : line_number_(0), error_code_(0)
	{
		load(stream, filename);
	}

	std::shared_ptr<const Attributes> defaults() const
	{
		return defaults_;
	}

	const Container &attributes() const
	{
		return attributes_;
	}

	const_iterator begin() const
	{
		return attributes_.begin();
	}

	const_iterator end() const
	{
		return attributes_.end();
	}

	bool load(std::istream &input, const char *source = "source")
	{
		reset_state(source);
		try {
			parse(input);

		} catch (const Exception &ex) {
			status_ = source_;
			if (line_number_) {
				char t_line_number[64] = {0};
				snprintf(t_line_number, sizeof(t_line_number)-1, " (%u)", line_number_);
				status_ += t_line_number;
			}
			status_ += " : ";
			status_ += ex.what();
			error_code_ = static_cast<int>(ex.error_code);
			return false;
		}
		line_number_ = 0;
		return true;
	}

	bool good() const
	{
		return (0 == error_code_);
	}

	const std::string &status(int &error_code) const
	{
		error_code = error_code_;
		return status_;
	}

	void clear_status()
	{
		status_.clear();
		error_code_ = 0;
	}

	void error(Exception::Type error_code, const char *fmt, ...)
	{
		if (error_code_)
			return;

		char buffer[1024];
		va_list ap;

		va_start(ap, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), fmt, ap);
		va_end(ap);

		if (0 == error_code) error_code = Exception::SYNTAX_ERROR;
		error_code_ = static_cast<int>(error_code);
		status_ += " : ";
		status_ += buffer;
	}

	static bool valid_symbol(const std::string &name)
	{
		return std::find_if(name.begin(), name.end(), [](char c) {
					return !(std::isalnum(c) || c == '_');
				}) == name.end();
	}

    private:
	void reset_state(const char *source)
	{
		source_ = (source ? source : "xconf");
		line_number_ = 0;
		defaults_.reset();
		attributes_.clear();
		clear_status();
	}

	//
	//  defaults
	//  {
	//	<attribute> <assign_op> <value> [<value> ...]
	//	...
	//  }
	//
	//  service <service_name>
	//  {
	//	<attribute> <assign_op> <value> [<value> ...]
	//	...
	//  }
	//
	void parse(std::istream &input)
	{
		std::shared_ptr<Attributes> attributes;
		std::string line, service_name;
		Attributes *defaults = nullptr;

		if (! input) {
			throw Exception::File("unable to open source");
		}

		while (std::getline(input, line)) {

			++line_number_;

			// any line whose first non-white-space character is a '#' is considered a comment line.
			// empty lines are ignored.
			ltrim(line);
			if (line.empty() || line[0] == '#') {
				continue;
			}

			// section
			if (! attributes) {
				if (service_name.empty()) {
						if (is_keyword(line, "includedir", 10)) {
							parse_directory(ltrim(line.erase(0, 11)).c_str());
							continue;

						} else if (is_keyword(line, "include", 7)) {
							parse_include(ltrim(line.erase(0, 8)).c_str());
							continue;
						}

						service_name = rtrim(line);
						if (service_name == "defaults") {
							if (! defaults_)
								continue;
							throw Exception::Section("duplicate defaults section");

						} else if (is_keyword(service_name, "service", 7)) {
							service_name.erase(0, 8);
							ltrim(service_name);
							if (service_name.empty())
								throw Exception::Section("missing service name");
							if (! valid_symbol(service_name))
								throw Exception::Section("invalid service name");
							continue;
						}

						if (service_name == "{")
							throw Exception::Section("missing section name");
						throw Exception::Section("unknown section");

				} else if ("{" == rtrim(line)) {
						attributes = std::make_shared<Attributes>(service_name);
						if (service_name == "defaults") {
							defaults_ = attributes;
							defaults = nullptr;
						} else {
							attributes_.push_back(attributes);
							defaults = defaults_.get();
						}
						continue;
				}
				throw Exception::Section("missing opening bracket");

			} else if (line[0] == '}') {
				if ("}" == rtrim(line)) {
					attributes.reset();
					service_name.clear();
					continue;
				}
				throw Exception::Section("invalid closing");
			}

			// key = value
			size_t eqs = line.find_first_of("="), eqe = eqs;
			char op = '=';

			if (std::string::npos == eqs) {
				if (line == "defaults" || is_keyword(line, "service", 7))
					throw Exception::Section("missing trailing bracket");
				throw Exception::Attribute("missing operator");
			}

			if (eqs && (line[eqs-1] == '+' || line[eqs-1] == '-'))
				op = line[--eqs]; // += or -=

			if ((eqs && !is_white(line[eqs-1])) || (++eqe < line.size() && !is_white(line[eqe++])))
				throw Exception::Attribute("invalid operator");

			std::string key(line, 0, eqs);
			std::string val(line, eqe, line.size() - eqe);

			rtrim(key);
			if (key.empty())
				throw Exception::Attribute("missing attribute key");		
			if (!valid_symbol(key))
				throw Exception::Attribute("invalid attribute key");

			trim(val);
			if (val.empty())
				throw Exception::Attribute("empty attribute value");

			if ('=' == op && attributes->find(key) != attributes->end())
				throw Exception::Attribute("mixed assignment operators");

			Attribute *attribute = new Attribute(key, op, val, Split(defaults));
			attributes->push_back(attribute);
		}

		if (attributes) {
			throw Exception::Section("missing closing bracket");
		}
	}

	struct ScopedDirectory {
		ScopedDirectory(const char *path) : d(opendir(path)) {
		}
		~ScopedDirectory() {
			if (d) closedir(d);
		}
		bool is_open() const {
			return (nullptr != d);
		}
		struct dirent *read() {
			return (d ? readdir(d) : nullptr);
		}
		DIR *d;
	};

	void parse_directory(const char *path)
	{
		if (!path || !*path) {
			throw Exception::Directory("missing directory");
		}

		ScopedDirectory dir(path);
		char t_path[1024] = {0};

		if (! dir.is_open()) {
			throw Exception::Directory("unable to open directory");
		}

		struct dirent *entry;
		while (nullptr != (entry = dir.read())) {
			const char *name = entry->d_name;
			if (name[0] == '.' || name[entry->d_namlen-1] == '~') {
				continue;
			}
			snprintf(t_path, sizeof(t_path), "%s/%s", path, name);
			parse_include(t_path);
		}
	}

	void parse_include(const char *path)
	{
		if (!path || !*path) {
			throw Exception::File("missing include");
		}

		const std::string t_source = source_;
		const unsigned t_line_number = line_number_;
		std::ifstream input(path);

		source_ = path;
		line_number_ = 0;
		parse(input);
		source_ = t_source;
		line_number_ = t_line_number;
	}

	static bool
	is_keyword(const std::string &line, const char *word, unsigned sz)
	{
		return (0 == line.compare(0, sz, word) && (sz == line.size() || is_white(line[sz])));
	}

	// is_white
	static bool
	is_white(const char ch)
	{
		return (' ' == ch || '\t' == ch || '\n' == ch || '\r' == ch);
	}

    public:
	// left trim
	static inline std::string&
	ltrim(std::string &s, const char *c = " \t\n\r")
	{
		s.erase(0, s.find_first_not_of(c));
		return s;
	}

	// right trim
	static inline std::string&
	rtrim(std::string &s, const char *c = " \t\n\r")
	{
		s.erase(s.find_last_not_of(c) + 1);
		return s;
	}

	// trim
	static inline std::string&
	trim(std::string &s, const char *c = " \t\n\r")
	{
		return ltrim(rtrim(s, c), c);
	}

    private:
	std::shared_ptr<Attributes> defaults_;
	Container attributes_;
	std::string source_;
	std::string status_;
	unsigned line_number_;
	int error_code_;
};


class ParserImpl;
class  Parser {
    public:
	Parser(std::istream &stream, const char *filename = "source");
	~Parser();

	struct servconfig *next(const struct configparams *param);
	const char *default(const char *key, char &op, unsigned idx) const;
	const char *status(int &error_code) const;
	bool good() const;

    private:
	friend class ParserImpl;
	ParserImpl *impl_;
};

};  //namespace xinetd

//end
