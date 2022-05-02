/* -*- mode: c; indent-width: 8; -*- */
/*
 * windows inetd service - netaddrs.
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

#if (0)

    Access conditions:

        Only_from and no_access effectively perform the same task, which is to block (no access) or
        permit (only_from) specific address or ranges of addresses. It is advisable to use one of
        these options to block everything by default and then build up a list of services lower
        down in the configuration file. By this strategy, you cover yourself if an event you
        didn’t account for occurs.

        These two options are also valid commands to include in the services section. So you can
        start off banning everything by default and then add in services. If there is a service
        section that relates to the connection request type that xinetd receives, it won’t look
        at the defaults section of the configuration.

        The instructions of only_from and no_access in a description for a service overrides the
        only_from and no_access statements in the defaults section.

        The format for these two options is

            <only_from | no_access> = <address | address range>

        Remember that the addresses can be expressed as IP addresses, hostnames, or domain names.
        However, it is better to stick to IP addresses. You can use CIDR notation to specify a range.
        Here are two examples of how you might use these options:

            no_access = 0.0.0.0/0

        This is probably the most common line in the defaults section because it blocks everyone.
        The defaults section is only there in the configuration file to tell xinetd what to do in
        the event of a service request that isn’t covered in the services section. You should work
        on the assumption that you will be able to provide specific instructions for every service
        type that your computer can provide, so it is reasonable to state that all other requests
        are blocked. As the doorman at an exclusive VIP party would say,
            “If you’re not on the list, you’re not getting in.”

        An alternative to this strategy is to let everyone in. You would implement this with:

            only_from = 0.0.0.0/0

        This policy doesn’t really make sense in the defaults section. The default section only
        gets referred to if you haven’t put in instructions for a service, so when xinetd resorts
        to the defaults, it has got a case that doesn’t have any instructions provided for it.
        So, allowing access in those circumstances would result in an error because you haven’t
        told xinetd what to do with the request. It is logical to use this catch-all only_from
        option within the description of a service, so this message would tell xinetd to allow
        requests from every possible source to use that service.

        Unfortunately, there is a feature of the only_from and no_access options that would
        create a conflict if you implemented a policy as described above. That is, both no_access
        and only_from are global and xinetd checks both of them every time it has a task to perform.
        So if you have both set, the daemon will validate the incoming request against that no_access
        statement in the defaults section even though there is a valid service configuration set up.

        This quirk of no_access and only_from being global can be overcome by deciding on a
        policy of only ever using one or the other in your xinetd.conf file. It is common practice
        to stick with only_from and ignore the no_access option. You can create a catch-all
        only instruction by leaving the address list blank in the defaults section,
        i.e., "only_from = " and that will let the xinetd program use the only_from setting
        in the services descriptions. This will occur without raising a conflict because the
        defaults section only_from value gets overwritten by the service`s only_from setting.

        Annoyingly, if you don`t put an only_from or a no_access statement in the defaults section,
        xinetd will allow all connections that you haven`t covered in the services section,
        which will probably create an error.

        The format for listing several addresses as parameters of both of these options is
        to leave a space between each address (no commas). You can also include CIDR ranges
        in the list.

#endif

#include "inetd.h"
#include <syslog.h>

#include <algorithm>

#include "accessip.h"

static inetd::CriticalSection netaddr_lock;


netaddrs::netaddrs()
	: match_default_(0), table_(nullptr)
{
}


netaddrs::netaddrs(const netaddrs &rhs)
	: match_default_(0), table_(nullptr)
{
	addresses_ = rhs.addresses_;
}


netaddrs&
netaddrs::operator=(netaddrs &&rhs)
{
	if (this != &rhs) {
		addresses_ = std::move(rhs.addresses_);
		rhs.reset();
		reset();
	}
	return *this;
}


netaddrs::~netaddrs()
{
	clear();
}


const netaddrs::Collection&
netaddrs::operator()() const
{
	return addresses_;
}


int
netaddrs::match_default() const
{
	return match_default_;
}


bool
netaddrs::match_default(int status)
{
	if (status && match_default_) {
		return ((match_default_ < 0 && status < 0) || (match_default_ > 0 && status > 0));
	}
	match_default_ = status;
	return true;
}


bool
netaddrs::has_unspec(char op) const
{
	return std::find_if(addresses_.begin(), addresses_.end(), [&](const auto &element) {
			    return (op == element.op && AF_UNSPEC == element.addr.family);
			}) != addresses_.end();
}


bool
netaddrs::push(const netaddr &addr, char op)
{
	if (std::find_if(addresses_.begin(), addresses_.end(), [&](const auto &element) {
			    return (0 == netaddrcmp(&addr, &element.addr));
			}) != addresses_.end())
		return false; // non-unique

	addresses_.push_back({addr, op});
	return true;
}


bool
netaddrs::erase(const netaddr &addr, char op)
{
	std::remove_if(addresses_.begin(), addresses_.end(), [&](const auto &element) {
			    return (op == element.op && 0 == netaddrcmp(&addr, &element.addr));
			});
	return true;
}


void
netaddrs::sysdump() const
{
	for (const auto &address : addresses_) {
		const int masklen = getmasklength(&address.addr);
		char t_addr[64], t_mask[64];

		inet_ntop(address.addr.family, (void *)&address.addr.network, t_addr, sizeof(t_addr));
		inet_ntop(address.addr.family, (void *)&address.addr.mask, t_mask, sizeof(t_mask));
		syslog(LOG_DEBUG, "%c: %s/%d (%s)", address.op, t_addr, masklen, t_mask);
	}
}


size_t
netaddrs::size() const
{
	return addresses_.size();
}


bool
netaddrs::empty() const
{
	return addresses_.empty();
}


void
netaddrs::clear(char op)
{
	std::remove_if(addresses_.begin(), addresses_.end(), [&](const auto &element) {
			    return (op == element.op);
			});
}


void
netaddrs::clear()
{
	addresses_.clear();
	reset();
}


void
netaddrs::reset()
{
	delete table_;
	table_ = nullptr;
}


bool
netaddrs::build()
{
	inetd::CriticalSection::Guard guard(netaddr_lock);
	if (nullptr == table_) {
		table_ = new AccessIP(*this, match_default());
	}
	return true;
}


bool
netaddrs::allowed(const struct netaddr &addr) const
{
	if (0 == addr.family)
		return true;

	if (nullptr == table_) {
		inetd::CriticalSection::Guard guard(netaddr_lock);
		if (nullptr == table_) {
			table_ = new AccessIP(*this, match_default());
		}
	}
	return table_->allowed(addr);
}


bool
netaddrs::allowed(const struct sockaddr_storage *addr) const
{
	if (nullptr == addr)
		return true;

	if (nullptr == table_) {
		inetd::CriticalSection::Guard guard(netaddr_lock);
		if (nullptr == table_) {
			table_ = new AccessIP(*this, match_default());
		}
	}
	return table_->allowed(addr);
}


/////////////////////////////////////////////////////////////////////////////////////////
//  netaddr's

int
accessip(PeerInfo &remote)
{
	const struct servtab *sep = remote.getserv();
	return sep->se_addresses.allowed(remote.getaddr());
}

//end
