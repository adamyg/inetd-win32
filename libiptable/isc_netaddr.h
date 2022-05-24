#pragma once
#ifndef ISC_NETADDR_H_INCLUDED
#define ISC_NETADDR_H_INCLUDED

#include <sys/cdefs.h>

#include "netaddr.h"

__BEGIN_DECLS

struct isc_buffer;

typedef struct netaddr isc_netaddr_t;
typedef struct isc_buffer isc_buffer_t;

int  isc_netaddr_equal(const isc_netaddr_t *a, const isc_netaddr_t *b);
int  isc_netaddr_eqprefix(const isc_netaddr_t *a, const isc_netaddr_t *b, unsigned int prefixlen);
int  isc_netaddr_totext(const isc_netaddr_t *netaddr, char *buffer, size_t buflen);
int  isc_netaddr_prefixok(const isc_netaddr_t *na, unsigned int prefixlen);
int  isc_netaddr_masktoprefixlen(const isc_netaddr_t *s, unsigned int *lenp);
void isc_netaddr_fromin(isc_netaddr_t *netaddr, const struct in_addr *ina);
void isc_netaddr_fromin6(isc_netaddr_t *netaddr, const struct in6_addr *ina6);
int  isc_netaddr_frompath(isc_netaddr_t *netaddr, const char *path);
void isc_netaddr_any(isc_netaddr_t *netaddr);
void isc_netaddr_any6(isc_netaddr_t *netaddr);
void isc_netaddr_unspec(isc_netaddr_t *netaddr);
int  isc_netaddr_ismulticast(const isc_netaddr_t *na);
int  isc_netaddr_isexperimental(const isc_netaddr_t *na);
int  isc_netaddr_islinklocal(const isc_netaddr_t *na);
int  isc_netaddr_issitelocal(const isc_netaddr_t *na);
int  isc_netaddr_isnetzero(const isc_netaddr_t *na);
void isc_netaddr_fromv4mapped(isc_netaddr_t *t, const isc_netaddr_t *s);
int  isc_netaddr_isloopback(const isc_netaddr_t *na);

__END_DECLS

#endif /*ISC_NETADDR_H_INCLUDED*/

//end

