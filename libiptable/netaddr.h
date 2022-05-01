#pragma once
#ifndef NETADDR_H_INCLUDED
#define NETADDR_H_INCLUDED

#include <sys/cdefs.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

__BEGIN_DECLS

struct netaddr {
	int	family;
	int	length;
	int	zone;		/* inet6: scope id (new in RFC2553) */
	union {
		struct in_addr v4;
		struct in6_addr v6;
		uint64_t u64;
	} network;
	union {
		struct in_addr v4;
		struct in6_addr v6;
	} mask;

#define netaddr_v4addr network.v4
#define netaddr_v6addr network.v6
#define netaddr_viaddr network.u64
#define netaddr_v4mask mask.v4
#define netaddr_v6mask mask.v6
};

#define NETADDR__ADDRSTRLEN 46                  /* IP4=16, IP6=46 */

#define NETADDR_NUMERICHOST 0x0001		/* node must be a numerical network address. */
#define NETADDR_IMPLIEDMASK 0x0002		/* imply netmask, if omitted. */

int getnetaddr(const char *addr, struct netaddr *res, int family, unsigned flags);
int getnetaddrx(const char *addr, struct netaddr *res, int family, unsigned flags, char *buf, unsigned buflen);
int netaddrcmp(const struct netaddr *a1, const struct netaddr *a2);

int getmasklength(const struct netaddr *res);

__END_DECLS

#endif /*NETADDR_H_INCLUDED*/

//end