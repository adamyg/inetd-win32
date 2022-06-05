/* -*- mode: c; indent-width: 8; -*- */
/*
 * Configuration
 * windows inetd service -- xinetd style.
 *
 * Copyright (c) 2021 - 2022, Adam Young.
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

#define  NOMINMAX

#include "inetd.h"

#include <iostream>
#include <fstream>

#include <err.h>
#include <grp.h>
#include <pwd.h>

#include <syslog.h>
#include "config2.h"
#include "xinetd.h"

#include "../libiptable/isc_util.h"

/*
 *  defaults
 *  {
 *      <attribute> = <value> <value> ...
 *      ...
 *  }
 *
 *  service <service_name>
 *  {
 *      <attribute> <assign_op> <value> <value> ...
 *      ...
 *  }
 */

namespace xinetd {

class ParserImpl
{
    public:
	enum parse_status {
		Expected = -2,
		Disabled = -1,
		Failure  = false,
		Success  = true
	};

	struct KeyValue
	{
		const char *name;
		parse_status (*function)(ParserImpl &parser, const xinetd::Attribute *attr);
		unsigned options;

#define Optional	0x0000
#define Required	0x0100
#define Default 	0x0200
#define Modifier	0x1000
#define Multiple	0x2000
#define Upto(__x)	((__x) & 0xff)
	};

    public:
	ParserImpl(std::istream &stream, const char *filename);

	bool good() const;
	std::shared_ptr<const Attributes> defaults() const;
	struct servconfig *next(const struct configparams *params);
	const std::string &status(int &error_code) const;

	static parse_status socket_type(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status type(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status flags(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status protocol(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status wait(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status user(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status group(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status server(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status server_args(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status working_directory(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status id(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status instances(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status log_on_success(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status log_on_failure(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status log_type(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status access_times(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status rpc_version(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status rpc_number(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status port(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status redirect(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status bind(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status only_from(ParserImpl &parser, const xinetd::Attribute *attr);
	static bool only_from(ParserImpl &parser, char op, const std::string &value);
	static parse_status no_access(ParserImpl &parser, const xinetd::Attribute *attr);
	static bool no_access(ParserImpl &parser, char op, const std::string &value);
	static parse_status sndbuf(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status rcvbuf(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status geoip_database(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status geoip_allow(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status geoip_deny(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status socket_uid(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status socket_gid(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status socket_mode(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status passenv(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status env(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status banner(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status banner_success(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status banner_fail(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status per_source(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status cpm(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status enabled(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status disable(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status max_load(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status deny_time(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status ipsec_policy(ParserImpl &parser, const xinetd::Attribute *attr);
	static parse_status apply_defaults(ParserImpl &parser);

	void servwarn(const char *fmt, ...)
	{
		const struct servconfig *sep = &configent_;
		char buffer[1024];
		va_list ap;

		va_start(ap, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), fmt, ap);
		syslog(LOG_WARNING, "%s: %s", sep->se_service, buffer);
		va_end(ap);
	}

	void serverr(const char *fmt, ...)
	{
		const struct servconfig *sep = &configent_;
		char buffer[1024];
		va_list ap;

		va_start(ap, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), fmt, ap);
		if (! sep->se_proto.is_null()) {
			syslog(LOG_ERR, "%s/%s: %s", sep->se_service, sep->se_proto, buffer);
		} else {
			syslog(LOG_ERR, "%s: %s", sep->se_service, buffer);
		}
		va_end(ap);
	}

	void bad_attribute(const char *fmt, ...)
	{
		const struct servconfig *sep = &configent_;
		char buffer[1024];
		va_list ap;

		va_start(ap, fmt);
		_vsnprintf_s(buffer, sizeof(buffer), fmt, ap);
		collection_.error(Exception::INVALID_ATTRIBUTE, "%s: %s", sep->se_service, buffer);
		va_end(ap);
	}

	template <typename T>
	static bool strbase8(const char *str, T &value)
	{
		char *end = nullptr;
		value = (T)strtol(str, &end, 8);
		if (nullptr == end || *end)
			return false;
		return true;
	}

	template <typename T>
	static bool strbase10(const char *str, T &value)
	{
		char *end = nullptr;
		value = (T)strtol(str, &end, 10);
		if (nullptr == end || *end)
			return false;
		return true;
	}

	template <typename T>
	static bool strsize(const char *str, T &value)
	{
		char *end = nullptr;
		value = (T)strtol(str, &end, 10);
		if (end) {
			if (0 == *end)
				return true;
			if ('k' == *end || 'K' == *end) {
				value *= 1024;
				return true;
			} else if ('m' == *end || 'M' == *end) {
				value *= 1024 * 1024;
				return true;
			}
		}
		return false;
	}

#define WARNING_GEOIP_ALLOW	0x0001
#define WARNING_GEOIP_DENY	0x0002

	struct servconfig configent_;
	Collection collection_;
	Collection::const_iterator iterator_;
	const struct configparams *params_;
	unsigned warning_once_;
	int flag_family_;

    private:
	void process_defaults();
	struct servconfig *process_section(const Attributes &attributes, const std::shared_ptr<const Attributes> &defaults);
	bool attribute(const ParserImpl::KeyValue *k, unsigned element, const Attribute *attr);
	void reset();
};


/////////////////////////////////////////////////////////////////////////////////////////
//

static const ParserImpl::KeyValue service_attributes[] = {
	{ "socket_type",	ParserImpl::socket_type,	Required },
	{ "type",		ParserImpl::type,		Optional },
	{ "flags",		ParserImpl::flags,		Optional|Multiple },
	{ "protocol",		ParserImpl::protocol,		Optional },
	{ "wait",		ParserImpl::wait,		Required },
	{ "user",		ParserImpl::user,		Optional },
	{ "group",		ParserImpl::group,		Optional },
	{ "server",		ParserImpl::server,		Optional },
	{ "server_args",	ParserImpl::server_args,	Optional|Multiple },
	{ "working_directory",	ParserImpl::working_directory,	Optional },
	{ "id", 		ParserImpl::id, 		Optional },
	{ "instances",		ParserImpl::instances,		Default|Optional },
	{ "log_on_success",	ParserImpl::log_on_success,	Default|Optional|Multiple|Modifier },
	{ "log_on_failure",	ParserImpl::log_on_failure,	Default|Optional|Multiple|Modifier },
	{ "log_type",		ParserImpl::log_type,		Default|Optional|Upto(2) },
	{ "access_times",	ParserImpl::access_times,	Optional|Multiple },
	{ "rpc_version",	ParserImpl::rpc_version,	Optional },
	{ "rpc_number", 	ParserImpl::rpc_number, 	Optional },
	{ "port",		ParserImpl::port,		Optional },
	{ "bind",		ParserImpl::bind,		Default|Optional },
	{ "redirect",		ParserImpl::redirect,		Optional|Upto(2) },
	{ "only_from",		ParserImpl::only_from,		Default|Optional|Multiple|Modifier },
	{ "no_access",		ParserImpl::no_access,		Default|Optional|Multiple|Modifier },
	{ "sndbuf",		ParserImpl::sndbuf,		Default|Optional },
	{ "rcvbuf",		ParserImpl::rcvbuf,		Default|Optional },
	{ "geoip_database",	ParserImpl::geoip_database,	Default|Optional },
	{ "geoip_allow",	ParserImpl::geoip_allow,	Default|Optional|Multiple|Modifier },
	{ "geoip_deny",		ParserImpl::geoip_deny,		Default|Optional|Multiple|Modifier },
#if defined(HAVE_AF_UNIX)
	{ "socket_uid",		ParserImpl::socket_uid,		Optional },
	{ "socket_gid",		ParserImpl::socket_gid,		Optional },
	{ "socket_mode",	ParserImpl::socket_mode,	Optional },
#endif
	{ "passenv",		ParserImpl::passenv,		Default|Optional|Multiple|Modifier },
	{ "env",		ParserImpl::env,		Default|Optional|Multiple|Modifier },
	{ "per_source",		ParserImpl::per_source,		Default|Optional },
	{ "banner",		ParserImpl::banner,		Default|Optional },
	{ "banner_success",	ParserImpl::banner_success,	Default|Optional },
	{ "banner_fail",	ParserImpl::banner_fail,	Default|Optional },
	{ "cpm",		ParserImpl::cpm,		Default|Optional|Upto(2) },
	{ "enabled",		ParserImpl::enabled,		Default|Optional|Multiple },
	{ "disable",		ParserImpl::disable,		Default|Optional },
	{ "max_load",		ParserImpl::max_load,		Default|Optional },
	{ "deny_time",		ParserImpl::deny_time,		Optional|Multiple },
	{ "ipsec_policy",	ParserImpl::ipsec_policy,	Optional },
#ifdef LIBWRAP
	{ "libwrap",		ParserImpl::libwrap,		Optional },
#endif
	{ nullptr }
	};


namespace {
template<typename Pred>
bool parse_file(ParserImpl &parser, const xinetd::Attribute *attr, Pred pred)
{
	if (attr->values.size() < 2) {
		parser.serverr("FILE option, missing filename");
		return false;
	} else if (attr->values.size() > 2) {
		parser.serverr("FILE option, too many arguments");
		return false;
	}

	const char *filename = attr->values[1].c_str();
	std::ifstream stream(filename);
	if (stream.fail()) {
		parser.serverr("FILE option, unable to open source <%s>", filename);
		return false;
	}

	std::string line;
	line.reserve(1024);
	while (std::getline(stream, line)) {
		const size_t bang = line.find_first_of("#");
		if (bang != std::string::npos) {
			line.erase(bang);
		}
		Collection::trim(line);
		if (! line.empty()) {
			if (! pred(parser, attr->op, line)) {
				return false;
			}
		}
	}
	return true;
}
};


/////////////////////////////////////////////////////////////////////////////////////////
//

xinetd::Parser::Parser(std::istream &stream, const char *filename)
	: impl_(new xinetd::ParserImpl(stream, filename))
{
}


xinetd::Parser::~Parser()
{
	delete impl_;
}


struct servconfig *
xinetd::Parser::next(const struct configparams *params)
{
	if (impl_) {
		return impl_->next(params);
	}
	return nullptr;
}


const char *
xinetd::Parser::status(int &error_code) const
{
	if (impl_) {
		return impl_->status(error_code).c_str();
	}
	return "not initialised";
}


const char *
xinetd::Parser::defaults(const char *key, char &op, unsigned idx) const
{
	if (impl_) {
		const auto &defaults = impl_->defaults();
		if (defaults) {
			auto it = defaults->find(key);
			while (it != defaults->end()) {
				if (0 == idx--) {
					op = (*it)->op;
					return (*it)->value.c_str();
				}
				it = defaults->find_next(key, it);
			}
		}
	}
	return nullptr;
}


bool
xinetd::Parser::good() const
{
	if (impl_) {
		return impl_->good();
	}
	return false;
}


/////////////////////////////////////////////////////////////////////////////////////////
//

ParserImpl::ParserImpl(std::istream &stream, const char *filename)
	: collection_(stream, filename), params_(nullptr), flag_family_(0), warning_once_(0)
{
	process_defaults();
	if (good()) {
		iterator_ = collection_.begin();
	} else {
		int t_error_code = 0;
		syslog(LOG_ERR, "%s", collection_.status(t_error_code));
		iterator_ = collection_.end();
	}
}


bool
ParserImpl::good() const
{
	return collection_.good();
}


const std::string&
ParserImpl::status(int &error_code) const
{
	return collection_.status(error_code);
}


std::shared_ptr<const Attributes>
ParserImpl::defaults() const
{
	return collection_.defaults();
}


struct servconfig *
ParserImpl::next(const struct configparams *params)
{
	auto defaults = collection_.defaults();
	struct servconfig *ret = nullptr;

	params_ = params;
	collection_.clear_status();
	for (;;) {
		reset();
		if (iterator_ == collection_.end())
			break;

		const auto &attributes = *iterator_++;
		configent_.se_service = servconfig::newname(attributes->name().c_str());
		if (nullptr != (ret = process_section(*attributes, defaults))) {
			break;
		}
	}

	params_ = nullptr;
	return ret;
}


void
ParserImpl::process_defaults()
{
	if (! good())
		return;

	const auto &defaults = collection_.defaults();
	if (! defaults)
		return;

	configent_.se_service = servconfig::newname("defaults");
	configent_.se_accept = 1;

	for (const KeyValue *key = service_attributes; key->name; ++key) {
		if (0 == (Default & key->options)) {
			continue; // non-default, ignore
		}

		auto dit = defaults->find(key->name);
		if (dit != defaults->end()) {
			for (unsigned element = 1;; ++element) {
				if (! attribute(key, element, *dit))
					return;
				dit = defaults->find_next(key->name, dit);
				if (dit == defaults->end())
					break;
			}
		}
	}
}


struct servconfig *
ParserImpl::process_section(const Attributes &attributes, const std::shared_ptr<const Attributes> &defaults)
{
	for (const KeyValue *key = service_attributes; key->name; ++key) {
		const Attribute *attr = nullptr;
		auto ait = attributes.find(key->name);
		if (ait != attributes.end()) { // first value
			attr = *ait;

		} else if (Default & key->options) { // apply default?
			if (defaults) { // if available
				auto dit = defaults->find(key->name);
				if (dit != defaults->end()) { // one or more elements
					for (unsigned element = 1;; ++element) {
						if (! attribute(key, element, *dit))
							return nullptr;
						dit = defaults->find_next(key->name, dit);
						if (dit == defaults->end())
							break;
					}
					continue;
				}
			}
		}

		for (unsigned element = 1;; ++element) {
			if (! attribute(key, element, attr)) // none or more elements
				return nullptr;
			if (nullptr == attr)
				break;
			ait = attributes.find_next(key->name, ait);
			if (ait == attributes.end())
				break;
			attr = *ait; // next value
		}
	}

	if (Success != apply_defaults(*this))
		return nullptr;

	return &configent_;
}


bool
ParserImpl::attribute(const ParserImpl::KeyValue *k, unsigned element, const Attribute *attr)
{
	ParserImpl::parse_status status = Failure;

	if (nullptr == attr) {
		if (Required & k->options) {
			status = Expected;
		} else {
			status = k->function(*this, nullptr);
		}

	} else {
		if (2 == element && (0 == (Multiple & k->options))) {
			bad_attribute("duplicate <%s> attribute", k->name);
			return false;
		}

		const size_t count = attr->values.size();
		if (0 == (Modifier & k->options) && '=' != attr->op) {
			bad_attribute("<%s> unsupported operator", k->name);
		} else if (count > 1 && 0 == (Multiple & k->options) && count > (0xff & k->options)) {
			bad_attribute("too many values for <%s>", k->name);
		} else {
			status = k->function(*this, attr);
		}
	}

	if (Success != status) {
		if (good()) { // default diagnostics
			if (Expected == status) {
				bad_attribute("config element <%s> expected", k->name);
			} else if (Disabled != status) {
				bad_attribute("attribute <%s> error", k->name);
			}
		}
		return false;
	}
	return true;
}


void
ParserImpl::reset()
{
	freeconfig(&configent_);
	flag_family_ = 0;
}


ParserImpl::parse_status
ParserImpl::socket_type(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	assert(attr); // required

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (strcmp(arg, "stream") == 0) {
		sep->se_socktype = SOCK_STREAM;
	} else if (strcmp(arg, "dgram") == 0) {
		sep->se_socktype = SOCK_DGRAM;
	} else if (strcmp(arg, "rdm") == 0) {
		sep->se_socktype = SOCK_RDM;
	} else if (strcmp(arg, "seqpacket") == 0) {
		sep->se_socktype = SOCK_SEQPACKET;
	} else if (strcmp(arg, "raw") == 0) {
		sep->se_socktype = SOCK_RAW;
	} else {
		sep->se_socktype = -1;
		return Failure;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::type(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		sep->se_type = NORM_TYPE;
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "RPC") == 0) {
#if defined(RPC)
		sep->se_type = NORM_TYPE;
		sep->se_rpc = 1;
#else
		parser.serverr("rpc services not supported");
		return Failure;
#endif
	} else if (_stricmp(arg, "INTERNAL") == 0) {
		sep->se_type = INTERNAL_TYPE;
	} else if (_stricmp(arg, "TCPMUX") == 0) {
		sep->se_type = MUX_TYPE;
	} else if (_stricmp(arg, "TCPMUXPLUS") == 0) {
		sep->se_type = MUXPLUS_TYPE;
	} else if (_stricmp(arg, "UNLISTED") == 0) {
		sep->se_type = UNLISTED_TYPE;
	} else {
		parser.serverr("unknown service type <%s>", arg);
		return Failure;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::flags(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str();
		if (_stricmp(arg, "IPv4") == 0) {
			if (parser.flag_family_ && AF_INET != parser.flag_family_) {
				parser.serverr("IPV4 and IPV6 are mutually exclusive");
				return Failure;
			}
			parser.flag_family_ = AF_INET;

		} else if (_stricmp(arg, "IPv6") == 0) {
			if (parser.flag_family_ && AF_INET6 != parser.flag_family_) {
				parser.serverr("IPV4 and IPV6 are mutually exclusive");
				return Failure;
			}
			parser.flag_family_ = AF_INET6;

		} else if (_stricmp(arg, "LABELLED") == 0) {
			/*ignore, implied*/
		} else if (_stricmp(arg, "REUSE") == 0) {
			/*ignore, implied*/

//TODO		} else if (_stricmp(arg, "KEEPALIVE") == 0) {
//			sep->se_sockopts |= OPT_SO_KEEPALIVE;
//		} else if (_stricmp(arg, "SODEBUG") == 0) {
//			sep->se_sockopts |= OPT_SO_DEBUG;
//		} else if (_stricmp(arg, "NODELAY") == 0) {
//			sep->se_sockopts |= OPT_TCP_NODELAY;
//		} else if (_stricmp(arg, "SENSOR") == 0) {

		} else {
			parser.serverr("unknown flag <%s>", arg);
			return Failure;
		}
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::protocol(ParserImpl &parser, const xinetd::Attribute *attr)
{
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;
	const char *arg = "";

	if (nullptr == attr) {
		if (SOCK_STREAM == sep->se_socktype) {
			arg = "tcp";
		} else if (SOCK_DGRAM == sep->se_socktype) {
			arg = "udp";
		} else {
			arg = "";
		}
	} else {
		arg = attr->values[0].c_str();
	}

	bool v4bind = (AF_INET == parser.flag_family_);
	bool v6bind = (AF_INET6 == parser.flag_family_);

	sep->se_proto = arg;
	const char *se_proto = sep->se_proto;
	if (strncmp(se_proto, "tcp", 3) == 0) {
		//tcp[46]/faith
		if (nullptr != (arg = strchr(se_proto, '/')) &&
				strncmp(arg, "/faith", 6) == 0) {
			parser.serverr("faith has been deprecated");
			return Failure;
		}
	} else {
		//faith/xxx
		if (sep->se_type == NORM_TYPE &&
				strncmp(sep->se_proto, "faith/", 6) == 0) {
			parser.serverr("faith has been deprecated");
			return Failure;
		}
	}

#if defined(RPC)
	if (sep->se_rpc || strncmp(sep->se_proto, "rpc/", 4) == 0) {
		char *versp;

		if (strncmp(sep->se_proto, "rpc/", 4) == 0)
			memmove(sep->se_proto, sep->se_proto + 4, strlen(sep->se_proto) + 1 - 4);

		sep->se_rpc = 1;
		sep->se_rpc_prog = 0;
		sep->se_rpc_lowvers = 0;
		sep->se_rpc_highvers = 0;

		if ((versp = strrchr(sep->se_service, '/'))) {
			*versp++ = '\0';
			switch (sscanf(versp, "%u-%u", &sep->se_rpc_lowvers, &sep->se_rpc_highvers)) {
			case 2:
				break;
			case 1:
				sep->se_rpc_highvers = sep->se_rpc_lowvers;
				break;
			default:
				parser.serverr("bad RPC version specifier <%s>", versp);
				return Failure;
			}
		}
	}
#else
	if (strncmp(se_proto, "rpc/", 4) == 0) {
		parser.serverr("rpc services not supported");
		return Failure;
	}
#endif
	sep->se_nomapped = 0;

	if (strcmp(se_proto, "unix") == 0) {
#if defined(HAVE_AF_UNIX)
		sep->se_family = AF_UNIX;
#else
		parser.serverr("unix services not supported");
		return Failure;
#endif
	} else {
		int protolen = strlen(se_proto);
		char *proto = (char *) se_proto;
		if (0 == protolen) {
			parser.serverr("invalid protocol specified");
			return Failure;
		}

		while (protolen-- && isdigit(proto[protolen])) {
			if (proto[protolen] == '6') { // tcp6 or udp6
				proto[protolen] = '\0';
				v6bind = true;
				continue;
			}

			if (proto[protolen] == '4') { // tcp4 or udp4
				proto[protolen] = '\0';
				v4bind = true;
				continue;
			}

			/* illegal version num */
			parser.serverr("bad IP version for protocol");
			return Failure;
		}

		struct protoent *pe;
		if (nullptr == (pe = getprotobyname(se_proto))) {
			parser.serverr("unknown protocol <%s>", se_proto);
			return Failure;
		}

#ifndef INET6
		if (v6bind) {
			parser.serverr("IPv6 is not available, ignored");
			return Disabled;
		}
#endif

		if (v6bind && !params->v6bind_ok) {
			if (v4bind && params->v4bind_ok) {
				parser.serverr("IPv6 bind is ignored, reverting to IPV4");
				v6bind = false;
			} else {
				parser.serverr("IPv6 bind is disabled, ignored");
				return Disabled;
			}
		}

		if (v6bind) {
			sep->se_family = AF_INET6;
			if (!v4bind || !params->v4bind_ok)
				sep->se_nomapped = 1;
		} else
		{ /* default to v4 bind if not v6 bind */
			if (!params->v4bind_ok) {
				parser.serverr("IPv4 bind is disabled, ignored");
				return Disabled;
			}
			sep->se_family = AF_INET;
		}

		if ((SOCK_STREAM == sep->se_socktype && _stricmp(proto, "tcp") != 0) ||
		    (SOCK_DGRAM  == sep->se_socktype && _stricmp(proto, "udp") != 0)) {
			parser.servwarn("socket_type and protocol <%s> inconsistent", proto);
		}
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::wait(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	assert(attr); // required

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "yes") == 0) {
		sep->se_accept = 0;
	} else if (_stricmp(arg, "no") == 0) {
		sep->se_accept = 1;
	} else {
		parser.serverr("invalid wait value <%s>", arg);
		return Failure;
	}

	if (ISMUX(sep)) {
		/*
		 * Silently enforce "nowait" mode for TCPMUX services
		 * since they don't have an assigned port to listen on.
		 */
		sep->se_accept = 1;
		if (strcmp(sep->se_proto, "tcp")) {
			parser.serverr("bad protocol for tcpmux service");
			return Failure;
		}
		if (sep->se_socktype != SOCK_STREAM) {
			parser.serverr("bad socket type for tcpmux service");
			return Failure;
		}
	}

	return Success;
}


ParserImpl::parse_status
ParserImpl::user(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_user = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::group(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_group = arg;
	return Success;
}


static int
matchservent(const char *name1, const char *name2, const char *proto)
{
	char **alias;
	const char *p;
	struct servent *se;

	if (strcmp(proto, "unix") == 0) {
		if ((p = strrchr(name1, '/')) != NULL)
			name1 = p + 1;
		if ((p = strrchr(name2, '/')) != NULL)
			name2 = p + 1;
	}
	if (strcmp(name1, name2) == 0)
		return(1);
	if ((se = getservbyname(name1, proto)) != NULL) {
		if (strcmp(name2, se->s_name) == 0)
			return(1);
		for (alias = se->s_aliases; *alias; alias++)
			if (strcmp(name2, *alias) == 0)
				return(1);
	}
	return(0);
}


ParserImpl::parse_status
ParserImpl::server(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;

	if (INTERNAL_TYPE == sep->se_type) {
		const struct biltin *bi;

		if (attr) { /* optional */
			const char *arg = attr->values[0].c_str();
			if (0 == strcmp(arg, "internal")) {
				parser.serverr("server expected as <internal>");
				return Failure;
			}
		}

		for (bi = biltins; bi->bi_service; ++bi) {
			if (bi->bi_socktype == sep->se_socktype &&
				    matchservent(bi->bi_service, sep->se_service, sep->se_proto)) {
				break;
			}
		}

		if (nullptr == bi->bi_service) {
			parser.serverr("internal service unknown");
			return Failure;
		}

		sep->se_server = "internal"; /* implied */
		sep->se_accept = 1; /* force accept mode for built-ins */
		sep->se_bi = bi;
		return Success;
	}

	if (nullptr == attr)
		return Expected;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_server = arg;
	if (nullptr != (sep->se_server_name = strrchr(sep->se_server.c_str(), '/')))
		++sep->se_server_name;

	return Success;
}


ParserImpl::parse_status
ParserImpl::server_args(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		sep->se_arguments = "";
		memset(sep->se_argv, 0, sizeof(sep->se_argv));
		return Success;
	}

	if (attr->values.size() > MAXARGV) {
		parser.serverr("too many arguments for service");
		return Failure;
	}

	size_t argc = 0;
	sep->se_arguments = attr->value.c_str();
	while (argc < attr->values.size()) {
		const char *arg = attr->values[argc].c_str();
		sep->se_argv[argc] = servconfig::newarg(arg);
		++argc;
	}
	while (argc <= MAXARGV)
		sep->se_argv[argc++] = nullptr;
	return Success;
}


ParserImpl::parse_status
ParserImpl::working_directory(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_working_directory = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::id(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_server_name = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::instances(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	sep->se_maxchild = -1;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "UNLIMITED") == 0) {
		sep->se_maxchild = 0;
	} else {
		long instances = 0;
		if (! parser.strbase10(arg, instances) || instances < 1 || instances > MAX_MAXCHLD) {
			parser.serverr("bad instances <%s>", arg);
			return Failure;
		}
		sep->se_maxchild = (int)instances;
	}
	if (debug && !sep->se_accept && sep->se_maxchild != 1)
		parser.servwarn("maxchild=%s for wait service not recommended", arg);
	return Success;
}


ParserImpl::parse_status
ParserImpl::log_on_success(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// PID HOST USERID EXIT DURATION TRAFFIC
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str();
		if (strcmp(arg, "PID") == 0) {
		} else if (strcmp(arg, "HOST") == 0) {
		} else if (strcmp(arg, "USERID") == 0) {
		} else if (strcmp(arg, "EXIT") == 0) {
		} else if (strcmp(arg, "DURATION") == 0) {
		} else if (strcmp(arg, "TRAFFIC") == 0) {
		} else {
			parser.serverr("unknown log_on_success option <%s>", arg);
			return Failure;
		}
	}

	//TODO, LOG
	return Success;
}


ParserImpl::parse_status
ParserImpl::log_on_failure(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// HOST USERID ATTEMPT
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str();
		if (strcmp(arg, "HOST") == 0) {
		} else if (strcmp(arg, "USERID") == 0) {
		} else if (strcmp(arg, "ATTEMPT") == 0) {
		} else if (strcmp(arg, "RECORD") == 0) {
		} else {
			parser.serverr("unknown log_on_failure option <%s>", arg);
			return Failure;
		}
	}

	//TODO, LOG
	return Success;
}


ParserImpl::parse_status
ParserImpl::log_type(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// FILE file [soft_limit [hard_limit]]
	// SYSLOG syslog_facility [syslog_level]
	// EVENTLOG ...
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(attr->values.size() >= 1);
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "FILE") == 0) {
	} else if (_stricmp(arg, "SYSLOG") == 0) {
	} else if (_stricmp(arg, "EVENTLOG") == 0) {
	} else {
		parser.serverr("invalid log_type <%s>", arg);
		return Failure;
	}

	//TODO, LOG
	return Success;
}


ParserImpl::parse_status
ParserImpl::access_times(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// 2:00-9:00 12:00-24:00
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str();
		access_times::time range;

		if (! access_times::to_access_range(arg, range)) {
			parser.serverr("bad access_time format <%s>", arg);
			return Failure;

		} else if (! sep->se_access_times.push(range)) {
			parser.serverr("too many access_time elements <%s>", arg);
			return Failure;
		}
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::rpc_version(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
#if defined(RPC)
		if (sep->se_rpc && 0 == sep->se_rpc_highvers) {
			sep->se_rpc_highvers = 1;
			sep->se_rpc_highlows = 1;
		}
#endif
		return Success;
	}

#if defined(RPC)
	if (sep->se_rpc) {
		const char *arg = attr->ait[0].c_str();
		unsigned lowvers = 0, highvers = 0;

		switch (sscanf(arg, "%u-%u", &lowvers, &highvers)) {
		case 2:
			break;
		case 1:
			highvers = lowvers;
			break;
		default:
			parser.serverr("bad RPC version specifier <%s>", arg);
			return Failure;
		}
		sep->se_rpc_highvers = highvers;
		sep->se_rpc_highvers = lowvers;
	}
#endif
	return Success;
}


ParserImpl::parse_status
ParserImpl::rpc_number(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
#if defined(RPC)
		if (sep->se_rpc && UNLISTED_TYPE == sep->se_type) {
			return Expected;
		}
#endif
		return Success;
	}

#if defined(RPC)
	if (sep->se_rpc && UNLISTED_TYPE == sep->se_type) {
		const char *arg = attr->ait[0].c_str();
		long prog = 0;
		if (! parser.strbase10(arg, prog) || prog <= 0) {
			parser.serverr("invalid rpc_number <%s>", arg);
			return Failure;
		}
		sep->se_prog = (int)prog;
	}
#endif	//TODO

	return Success;
}


ParserImpl::parse_status
ParserImpl::port(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;

#if defined(RPC)
	if (!sep->se_rpc) {
#endif
#if defined(HAVE_AF_UNIX)
		if (AF_UNIX != sep->se_family) {
#endif
			struct servent *sp;
			sp = getservbyname(sep->se_service, sep->se_proto);
			if (nullptr == sp) {
				if (UNLISTED_TYPE != sep->se_type) {
					parser.serverr("unknown service");
					return Failure;
				}
				sep->se_port = 0;
			} else {
				sep->se_port = sp->s_port;
			}
#if defined(HAVE_AF_UNIX)
		}
#endif
#if defined(RPC)
	}
#endif

	if (nullptr == attr) {
		if (UNLISTED_TYPE == sep->se_type) {
			return Expected;
		}
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	switch(sep->se_family) {
	case AF_INET:
#ifdef INET6
	case AF_INET6:
#endif
		long port = 0;
		if (! parser.strbase10(arg, port) || port < 0 || port > 0xffff) {
			parser.serverr("invalid port <%s>", arg);
			return Failure;
		}
		sep->se_port = htons((unsigned short)port);
		break;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::bind(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// bind = 10.0.2.15
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;

	const struct sockaddr_in *bind_sa4 = params->bind_sa4;
	const struct sockaddr_in6 *bind_sa6 = params->bind_sa6;
	struct addrinfo *result = nullptr;

	if (attr) {
		const char *arg = attr->values[0].c_str();
		struct addrinfo hints;
		int ret;

		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_DGRAM;

		if (0 == (ret = getaddrinfo(arg, NULL, &hints, &result))) {
			struct addrinfo *ai = result;
			do {
				if (nullptr == ai->ai_addr) {
					parser.serverr("bind <%s> getaddrinfo failed", arg);
					return Failure;
				}

				if (sep->se_family != ai->ai_addr->sa_family) {
					continue;
				}

				switch (ai->ai_addr->sa_family) {
				case AF_INET:
					bind_sa4 = (struct sockaddr_in *) ai->ai_addr;
					break;
#ifdef INET6
				case AF_INET6:
					bind_sa6 = (struct sockaddr_in6 *) ai->ai_addr;
					break;
#endif
				default:
					freeaddrinfo(result);
					return Failure;
				}
				break;

			} while (nullptr != (ai = ai->ai_next));

			if (nullptr == ai) {
				parser.serverr("bind <%s> inconsistent address type", arg);
				return Failure;
			}

		} else {
			parser.serverr("bind <%s> %s", arg, gai_strerror(ret));
			return Failure;
		}
	}

	switch(sep->se_family) {
	case AF_INET:
		memcpy(&sep->se_ctrladdr4, bind_sa4, sizeof(sep->se_ctrladdr4));
		sep->se_ctrladdr4.sin_port = sep->se_port;
		sep->se_ctrladdr_size = sizeof(sep->se_ctrladdr4);
		break;
#ifdef INET6
	case AF_INET6:
		memcpy(&sep->se_ctrladdr6, bind_sa6, sizeof(sep->se_ctrladdr6));
		sep->se_ctrladdr6.sin6_port = sep->se_port;
		sep->se_ctrladdr_size = sizeof(sep->se_ctrladdr6);
		break;
#endif
#if defined(HAVE_AF_UNIX)
	case AF_UNIX:
#define SUN_PATH_MAXSIZE	sizeof(sep->se_ctrladdr_un.sun_path)
		memset(&sep->se_ctrladdr, 0, sizeof(sep->se_ctrladdr));
		sep->se_ctrladdr_un.sun_family = sep->se_family;
		if ((unsz = strlcpy(sep->se_ctrladdr_un.sun_path,
				sep->se_service, SUN_PATH_MAXSIZE) >= SUN_PATH_MAXSIZE)) {
			parser.syerr("domain socket pathname too long for service");
			return Failure;
		}
		sep->se_ctrladdr_un.sun_len = unsz;
#undef SUN_PATH_MAXSIZE
		sep->se_ctrladdr_size = SUN_LEN(&sep->se_ctrladdr_un);
		break;
#endif //HAVE_AF_UNIX
	}

	if (result)
		freeaddrinfo(result);

	return Success;
}


ParserImpl::parse_status
ParserImpl::redirect(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// redirect = 10.0.2.16 [1234]
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	if (AF_INET != sep->se_family && AF_INET6 != sep->se_family)
		return Success;

	if (attr->values.size()) {
		const char *arg = attr->values[0].c_str();
		struct addrinfo hints, *ai;
		int ret_ga;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_flags = AI_CANONNAME;

		if (0 == (ret_ga = getaddrinfo(arg, NULL, &hints, &ai))) {
			switch (ai->ai_family) {
			case AF_INET:
				sep->se_remote_family = AF_INET;
				sep->se_remoteaddr4 = *((struct sockaddr_in *) ai->ai_addr);
				sep->se_remoteaddr_size = sizeof(sep->se_remoteaddr4);
				sep->se_remote_name = ai->ai_canonname;
				break;
#ifdef INET6
			case AF_INET6:
				sep->se_remote_family = AF_INET6;
				sep->se_remoteaddr6 = *((struct sockaddr_in6 *) ai->ai_addr);
				sep->se_remoteaddr_size = sizeof(sep->se_remoteaddr6);
				sep->se_remote_name = ai->ai_canonname;
				break;
#endif
			default:
				break;
			}
			freeaddrinfo(ai);
		} else {
			return Failure;
		}
	}

	sep->se_remote_port = sep->se_port;
	if (2 == attr->values.size()) {
		const char *arg = attr->values[1].c_str();
		long port = 0;

		if (! parser.strbase10(arg, port) || port < 0 || port > 0xffff) {
			parser.serverr("invalid port <%s>", arg);
			return Failure;
		}
		sep->se_remote_port = htons((unsigned short)port);
	}

	//TODO, REDIRECT
	return Success;
}


ParserImpl::parse_status
ParserImpl::only_from(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// only_from = 192.168.1.107 ...
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	if (attr->values.size() && (AF_INET == sep->se_family || AF_INET6 == sep->se_family)) {
		if ('=' == attr->op) {
			sep->se_addresses.clear('+');
		}

		if (strcmp(attr->values[0].c_str(), "FILE") == 0) {
			if (!parse_file(parser, attr,
					[&](auto &parser, char op, const std::string &value) -> bool {
						return only_from(parser, op, value);
					})) {
				return Failure;
			}
		} else {
			for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
				if (! only_from(parser, attr->op, attr->values[vi])) {
					return Failure;
				}
			}
		}
	}
	return Success;
}


bool
ParserImpl::only_from(ParserImpl &parser, char op, const std::string &value)
{
	// only_from = 192.168.1.107 ...
	struct servconfig *sep = &parser.configent_;
	auto &addresses = sep->se_addresses;
	struct netaddr addr = {0};
	char errmsg[512];

	if (_stricmp(value.c_str(), "ALL") == 0) { // wild-card
		if (! addresses.match_default(1)) {
			parser.serverr("invalid only_from/no_access=ALL are mutually exclusive");
			return false;
		}
		return true;
	}

	if (! getnetaddrx(value.c_str(), &addr, sep->se_family, NETADDR_IMPLIEDMASK, errmsg, sizeof(errmsg))) {
		parser.serverr("invalid only_from address <%s>", value, errmsg);
		return false;
	}

	if ('-' != op) {
		if (! addresses.push(addr, '+')) {
			parser.serverr("non-unique only_from address <%s>", value.c_str());
			return false;
		}
	} else {
		addresses.erase(addr, '+');
	}
	return true;
}


ParserImpl::parse_status
ParserImpl::no_access(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// no_access = 10.0.1.0/24 ...
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	if (attr->values.size() && (AF_INET == sep->se_family || AF_INET6 == sep->se_family)) {
		if ('=' == attr->op) {
			sep->se_addresses.clear('-');
		}

		if (strcmp(attr->values[0].c_str(), "FILE") == 0) {
			if (!parse_file(parser, attr,
					[&](auto &parser, char op, const std::string &value) -> bool {
						return no_access(parser, op, value);
					})) {
				return Failure;
			}
		} else {
			for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
				if (! no_access(parser, attr->op, attr->values[vi])) {
					return Failure;
				}
			}
		}
	}
	return Success;
}


bool
ParserImpl::no_access(ParserImpl &parser, char op, const std::string &value)
{
	// no_access = 10.0.1.0/24 ...
	struct servconfig *sep = &parser.configent_;
	auto &addresses = sep->se_addresses;
	struct netaddr addr = {0};
	char errmsg[512];

	if (_stricmp(value.c_str(), "ALL") == 0) { // wild-card
		if (! addresses.match_default(-1)) {
			parser.serverr("invalid only_from/no_access=ALL are mutually exclusive");
			return false;
		}
		return true;
	}

	if (! getnetaddrx(value.c_str(), &addr, sep->se_family, NETADDR_IMPLIEDMASK, errmsg, sizeof(errmsg))) {
		parser.serverr("invalid no_access address <%s>", value, errmsg);
		return false;
	}

	if ('-' != op) {
		if (! addresses.push(addr, '-')) {
			parser.serverr("non-unique no_access address <%s>", value.c_str());
			return false;
		}
	} else {
		addresses.erase(addr, '-');
	}
	return true;
}


ParserImpl::parse_status
ParserImpl::sndbuf(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	long size;

	if (! ParserImpl::strsize(arg, size)) {
		parser.serverr("invalid sndbuf value <%s>", arg);
		return Failure;
	}
	sep->se_sndbuf = (int)size;
	return Success;
}


ParserImpl::parse_status
ParserImpl::rcvbuf(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	long size;

	if (! ParserImpl::strsize(arg, size)) {
		parser.serverr("invalid rcvbuf value <%s>", arg);
		return Failure;
	}
	sep->se_rcvbuf = (int)size;
	return Success;
}


ParserImpl::parse_status
ParserImpl::geoip_database(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		return Success;
	}

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_geoips.database(arg);
	return Success;
}


ParserImpl::parse_status
ParserImpl::geoip_allow(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	const auto& values = attr->values;
	if ('-' == attr->op) {
		parser.serverr("operator -= not applicable for geoip_allow");
		return Failure;
	} else if ('=' == attr->op) {
		sep->se_geoips.clear('+');
	}

#if defined(HAVE_LIBMAXMINDDB)
	if (_stricmp(values[0].c_str(), "ALL") == 0) { // wild-card
		if (! sep->se_geoips.match_default(1)) {
			parser.serverr("invalid geoip_allow/deny=ALL are mutually exclusive");
			return Failure;

		} else if (attr->values.size() > 1) {
			parser.serverr("unexpected geoip_allow trailing value(s) <%s ... >", values[1].c_str());
			return Failure;
		}

	} else if (! sep->se_geoips.push(values, '+')) {
		parser.serverr("invalid geoip_allow value <%s>", attr->value.c_str());
		return Failure;
	}

#else //HAVE_LIBMAXMINDDB
	if (0 == (parser.warning_once_ & WARNING_GEOIP_ALLOW)) {
		parser.servwarn("geoip support not available; geoip_allow option ignored");
		parser.warning_once_ |= WARNING_GEOIP_ALLOW;
	}
#endif
	return Success;
}


ParserImpl::parse_status
ParserImpl::geoip_deny(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	const auto& values = attr->values;
	if ('-' == attr->op) {
		parser.serverr("operator -= not applicable for geoip_deny");
		return Failure;
	} else if ('=' == attr->op) {
		sep->se_geoips.clear('-');
	}

#if defined(HAVE_LIBMAXMINDDB)
	if (_stricmp(values[0].c_str(), "ALL") == 0) { // wild-card
		if (! sep->se_geoips.match_default(-1)) {
			parser.serverr("invalid geoip_allow/deny=ALL are mutually exclusive");
			return Failure;

		} else if (attr->values.size() > 1) {
			parser.serverr("unexpected geoip_deny trailing value(s) <%s ... >", values[1].c_str());
			return Failure;
		}

	} else if (! sep->se_geoips.push(values, '-')) {
		parser.serverr("invalid geoip_deny value <%s>", attr->value.c_str());
		return Failure;
	}

#else //HAVE_LIBMAXMINDDB
	if (0 == (parser.warning_once_ & WARNING_GEOIP_DENY)) {
		parser.servwarn("geoip support not available; geoip_deny option ignored");
		parser.warning_once_ |= WARNING_GEOIP_DENY;
	}
#endif
	return Success;
}


ParserImpl::parse_status
ParserImpl::socket_uid(ParserImpl &parser, const xinetd::Attribute *attr)
{
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		sep->se_sockuid = params->euid;
		return Success;
	}

	assert(1 == attr->values.size());
	const char *user = attr->values[0].c_str();
	struct passwd *pw;

	if (nullptr == (pw = getpwnam(user))) {
		parser.serverr("no such user <%s>", user);
		return Failure;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::socket_gid(ParserImpl &parser, const xinetd::Attribute *attr)
{
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		sep->se_sockgid = params->egid;
		return Success;
	}

	assert(1 == attr->values.size());
	const char *group = attr->values[0].c_str();
	struct group *gr;

	if (nullptr == (gr = getgrnam(group))) {
		parser.serverr("no such user <%s>", group);
		return Failure;
	}
	sep->se_sockgid = gr->gr_gid;
	return Success;
}


ParserImpl::parse_status
ParserImpl::socket_mode(ParserImpl &parser, const xinetd::Attribute *attr)
{
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		sep->se_sockmode = 0200;
		return Success;
	}

	assert(1 == attr->values.size());
	const char *perm = attr->values[0].c_str();
	long mode = 0;

	if (! parser.strbase8(perm, mode) || 0 == mode || mode > 0777) {
		parser.serverr("invalid mode <%s>", perm);
		return Failure;
	}
	sep->se_sockmode = (mode_t)mode;
	return Success;
}


ParserImpl::parse_status
ParserImpl::passenv(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// passenv = PATH ...
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	auto &passenv = sep->se_environ.passenv();
	if ('=' == attr->op) {
		passenv.clear();
	}

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str();
		auto it = std::find_if(passenv.begin(), passenv.end(), [&](auto &val) {
					return (0 == strcmp(arg, val.c_str()));
				});

		if ('-' == attr->op) {
			if (it != passenv.end()) {
				passenv.erase(it);
			}
		} else {
			if (it == passenv.end()) {
				passenv.push_back(arg);
			}
		}
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::env(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// env = "HOME=/cvs" ...
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	auto &setenv = sep->se_environ.setenv();
	if ('-' == attr->op) {
		parser.serverr("operator -= not applicable for env setting");
		return Failure;

	} else if ('=' == attr->op) {
		setenv.clear();
	}

	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const char *arg = attr->values[vi].c_str(), *end;
		if (nullptr == (end = strchr(arg, '='))) {
			parser.serverr("env setting missing '='");
			return Failure;
		}

		const size_t len = end - arg;
		auto it = std::find_if(setenv.begin(), setenv.end(), [&](auto &val) {
					const char *t_arg = val.c_str(), *t_end = strchr(t_arg, '=');
					size_t t_len = t_end - t_arg;

					if (len == t_len) {
						return (0 == memcmp(arg, t_arg, len));
					}
					return false;
				});

		if (it == setenv.end()) {
			setenv.push_back(arg);
		} else {
			it->assign(arg);
		}
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::per_source(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;

	sep->se_maxperip = -1;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	long maxperip;

	if (! parser.strbase10(arg, maxperip) || maxperip < 0) {
		parser.serverr("invalid per_source <%s>", arg);
		return Failure;
	}
	sep->se_maxperip = (int)maxperip;
	return Success;
}


ParserImpl::parse_status
ParserImpl::banner(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_banner = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::banner_success(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_banner_success = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::banner_fail(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	sep->se_banner_fail = arg;
	return Success;
}


ParserImpl::parse_status
ParserImpl::cpm(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;

	sep->se_cpmmax = -1;
	sep->se_cpmwait = -1;
	if (nullptr == attr)
		return Success;

	if (attr->values.size() >= 1) {
		const char *arg = attr->values[0].c_str();
		long maxcpm;

		if (! parser.strbase10(arg, maxcpm) || maxcpm < 0) {
			parser.serverr("invalid maxcpm <%s>", arg);
			return Failure;
		}
		sep->se_cpmmax = (int)maxcpm;
	}

	if (attr->values.size() >= 2) {
		const char *arg = attr->values[1].c_str();
		long cpmwait;

		if (! parser.strbase10(arg, cpmwait) || cpmwait < 0) {
			parser.serverr("invalid cpmwait seconds <%s>", arg);
			return Failure;
		}
		sep->se_cpmwait = (int)cpmwait;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::enabled(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr) {
		return Success;
	}

	const bool processing_defaults = (0 == strcmp(sep->se_service, "defaults"));
	for (unsigned vi = 0; vi < attr->values.size(); ++vi) {
		const auto &arg = attr->values[vi];
		if (processing_defaults) {
			if (! Collection::valid_symbol(arg)) {
				parser.bad_attribute("invalid service name <%s>", arg.c_str());
			}
		} else {
			if (0 == strcmp(sep->se_service, arg.c_str())) {
				return Success;
			}
		}
	}

	if (! processing_defaults) {
		parser.servwarn("service not enabled");
		return Disabled;
	}
	return Success;
}


ParserImpl::parse_status
ParserImpl::disable(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "yes") == 0) {
		parser.servwarn("service disabled");
		return Disabled;

	} else if (_stricmp(arg, "no") != 0) {
		parser.serverr("invalid disable value <%s>", arg);
		return Failure;
	}
	return Success;
}


// CPU max load.
ParserImpl::parse_status
ParserImpl::max_load(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

	//TODO, CPULOAD
	return Success;
}


// SENSOR trigger action.
ParserImpl::parse_status
ParserImpl::deny_time(ParserImpl &parser, const xinetd::Attribute *attr)
{
	// deny_time = FOREVER|NEVER|<minutes>
	struct servconfig *sep = &parser.configent_;

	if (nullptr == attr)
		return Success;

	assert(1 == attr->values.size());
	const char *arg = attr->values[0].c_str();
	if (_stricmp(arg, "FOREVER") == 0) {
	} else if (_stricmp(arg, "NEVER") == 0) {
	} else {
		char *end = nullptr;
		long minutes = strtol(arg, &end, 10);
		if (nullptr == end || *end || minutes <= 0) {
			parser.serverr("invalid deny_time <%s>", arg);
			return Failure;
		}
	}

	//TODO, SENSOR
	return Success;
}


ParserImpl::parse_status
ParserImpl::ipsec_policy(ParserImpl &parser, const xinetd::Attribute *attr)
{
	struct servconfig *sep = &parser.configent_;
	if (nullptr == attr)
		return Success;

#if (IPSEC)
	const char *arg = attr->ait[0].c_str();
	sep->se_policy = arg;
#endif

	return Success;
}


ParserImpl::parse_status
ParserImpl::apply_defaults(ParserImpl &parser)
{
	const struct configparams *params = parser.params_;
	struct servconfig *sep = &parser.configent_;

	if (sep->se_maxperip < 0)
		sep->se_maxperip = params->maxperip;

	if (sep->se_cpmmax < 0)
		sep->se_cpmmax = params->maxcpm;

	if (sep->se_maxchild < 0) {	/* apply default max-children */
		if (sep->se_bi && sep->se_bi->bi_maxchild >= 0)
			sep->se_maxchild = sep->se_bi->bi_maxchild;
		else if (sep->se_accept)
			sep->se_maxchild = std::max(params->maxchild, 0);
		else
			sep->se_maxchild = 1;
	}

	return Success;
}

}; // namespace xinetd

//end
