#include "endpoint.h"

#include <arpa/inet.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "error.h"

enum SError selecon_parse_endpoint(struct SEndpoint *ep, const char *address, int port) {
	if (ep == NULL || address == NULL || port < 0 || port > USHRT_MAX)
		return SELECON_INVALID_ARG;
	if (inet_pton(AF_INET6, address, &ep->ipv6.sin6_addr) > 0) {
		ep->af             = AF_INET6;
		ep->ipv6.sin6_port = port;
		ep->addr_len       = sizeof(ep->ipv6);
	} else if (inet_pton(AF_INET, address, &ep->ipv4.sin_addr) > 0) {
		ep->af            = AF_INET;
		ep->ipv4.sin_port = port;
		ep->addr_len      = sizeof(ep->ipv4);
	} else
		return SELECON_INVALID_ADDRESS;
	return SELECON_OK;
}

enum SError selecon_parse_endpoint2(struct SEndpoint *ep, const char *address) {
	if (ep == NULL || address == NULL)
		return SELECON_INVALID_ARG;
	if (strncmp(address, "file://", sizeof("file://") - 1) == 0) {
		ep->af = AF_UNIX;
		memset(ep->un.sun_path, '\0', sizeof(ep->un.sun_path) - 1);
		strncpy(ep->un.sun_path, address + sizeof("file://") - 1, sizeof(ep->un.sun_path) - 1);
		ep->addr_len = sizeof(ep->un);
		return SELECON_OK;
	}
	char *colon = strrchr(address, ':');
	if (colon == NULL)
		return SELECON_INVALID_ADDRESS;
	int port        = atoi(colon + 1);
	char *ip_str    = strndup(address, colon - address);
	enum SError ret = selecon_parse_endpoint(ep, ip_str, port);
	free(ip_str);
	return ret;
}

void selecon_endpoint_dump(FILE *fd, struct SEndpoint *ep) {
	if (ep == NULL) {
		fprintf(fd, "(null)");
		return;
	}
	char buf[1024];
	switch (ep->af) {
		case AF_INET:
			inet_ntop(ep->af, &ep->ipv4.sin_addr, buf, sizeof(buf));
			fprintf(fd, "%s:%d", buf, ep->ipv4.sin_port);
			break;
		case AF_INET6:
			inet_ntop(ep->af, &ep->ipv6.sin6_addr, buf, sizeof(buf));
			fprintf(fd, "%s:%d", buf, ep->ipv6.sin6_port);
			break;
		case AF_UNIX: fprintf(fd, "file://%s", ep->un.sun_path); break;
		default:      fprintf(fd, "unknown");
	}
}
