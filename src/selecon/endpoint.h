#pragma once

#include <netinet/in.h>
#include <stdio.h>
#include <sys/un.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SEndpoint {
	int port;
	union {
		sa_family_t af;
		struct sockaddr addr;
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
		struct sockaddr_un un;
	};
	socklen_t addr_len;
};

// parses ipv4 and ipv6 addresses
enum SError selecon_parse_endpoint(struct SEndpoint *ep, const char *address, int port);

// parses address in forms ip:port or file://[socket-path]
enum SError selecon_parse_endpoint2(struct SEndpoint *ep, const char *address);

void selecon_endpoint_dump(FILE *fd, struct SEndpoint *ep);

#ifdef __cplusplus
}  // extern "C"
#endif
