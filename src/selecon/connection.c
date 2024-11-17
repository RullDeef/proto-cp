#include "connection.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <unistd.h>

#include "config.h"
#include "endpoint.h"
#include "message.h"

struct SConnection {
	int fd;
	struct SEndpoint src_ep;
	struct SEndpoint dst_ep;
};

enum SError sconn_listen(struct SConnection **con, struct SEndpoint *ep) {
	if (con == NULL || ep == NULL)
		return SELECON_INVALID_ARG;
	*con           = calloc(1, sizeof(struct SConnection));
	(*con)->src_ep = *ep;
	(*con)->fd     = socket(ep->af, SOCK_STREAM, 0);
	if ((*con)->fd == -1) {
		perror("socket");
		goto socket_err;
	}
	if (bind((*con)->fd, &(*con)->src_ep.addr, (*con)->src_ep.addr_len) != 0) {
		perror("bind");
		goto con_err;
	}
	if (listen((*con)->fd, SELECON_DEFAULT_LISTEN_BUF) != 0) {
		perror("listen");
		goto con_err;
	}
	return SELECON_OK;
con_err:
	close((*con)->fd);
socket_err:
	free(*con);
	*con = NULL;
	return SELECON_CON_ERROR;
}

enum SError sconn_accept(struct SConnection *con, struct SConnection **out_con, int timeout_ms) {
	if (con == NULL || out_con == NULL || timeout_ms < 0)
		return SELECON_INVALID_ARG;
	struct pollfd pollfd;
	pollfd.fd     = con->fd;
	pollfd.events = POLLIN;
	int ret       = poll(&pollfd, 1, timeout_ms);
	if (ret == -1)
		return SELECON_CON_ERROR;
	else if (ret == 0)
		return SELECON_CON_TIMEOUT;
	int other_sock = accept(con->fd, &con->dst_ep.addr, &con->dst_ep.addr_len);
	if (other_sock == -1)
		return SELECON_CON_ERROR;
	if (*out_con == NULL)
		*out_con = malloc(sizeof(struct SConnection));
	**out_con      = *con;
	(*out_con)->fd = other_sock;
	return SELECON_OK;
}

enum SError sconn_connect(struct SConnection **con, struct SEndpoint *ep) {
	if (con == NULL || ep == NULL)
		return SELECON_INVALID_ARG;
	*con           = malloc(sizeof(struct SConnection));
	(*con)->dst_ep = *ep;
	(*con)->fd     = socket(ep->af, SOCK_STREAM, 0);
	if ((*con)->fd == -1) {
		perror("socket");
		goto socket_err;
	}
	printf("inviting to ");
	selecon_endpoint_dump(stdout, ep);
	printf("\n");
	if (connect((*con)->fd, &(*con)->dst_ep.addr, (*con)->dst_ep.addr_len) != 0) {
		perror("connect");
		goto con_err;
	}
	return SELECON_OK;
con_err:
	close((*con)->fd);
socket_err:
	free(*con);
	*con = NULL;
	return SELECON_CON_ERROR;
}

enum SError sconn_disconnect(struct SConnection **con) {
	if (con == NULL || *con == NULL)
		return SELECON_INVALID_ARG;
	close((*con)->fd);
	free(*con);
	*con = NULL;
	return SELECON_OK;
}

enum SError sconn_send(struct SConnection *con, struct SMessage *msg) {
	if (con == NULL || msg == NULL)
		return SELECON_INVALID_ARG;
	if (send(con->fd, msg, msg->size, 0) == -1) {
		perror("send");
		return SELECON_CON_ERROR;
	}
	return SELECON_OK;
}

enum SError sconn_recv(struct SConnection *con, struct SMessage **msg) {
	if (con == NULL || msg == NULL)
		return SELECON_INVALID_ARG;
	size_t size = 0;
	int ret     = recv(con->fd, &size, sizeof(size_t), 0);
	if (ret == -1) {
		perror("recv");
		return SELECON_CON_ERROR;
	} else if (ret == 0) {
		return SELECON_CON_HANGUP;
	}
	if (*msg == NULL || (*msg)->size < size) {
		message_free(msg);
		*msg = message_alloc(size);
	}
	ret = recv(con->fd, &(*msg)->type, size - sizeof(size), 0);
	if (ret == -1) {
		perror("recv");
		return SELECON_CON_ERROR;
	} else if (ret != size - sizeof(size)) {
		message_free(msg);
		return SELECON_CON_ERROR;
	}
	return SELECON_OK;
}

enum SError sconn_recv_one(struct SConnection **cons,
                           size_t count,
                           struct SMessage **msg,
                           size_t *ready_index,
                           int timeout_ms) {
	if (cons == NULL || count == 0 || msg == NULL || ready_index == NULL)
		return SELECON_INVALID_ARG;
	struct pollfd *pollfd = alloca(count * sizeof(struct pollfd));
	for (size_t i = 0; i < count; ++i) {
		pollfd[i].fd      = cons[i]->fd;
		pollfd[i].events  = POLLIN;
		pollfd[i].revents = 0;
	}
	int ret = poll(pollfd, count, timeout_ms);
	if (ret == -1)
		return SELECON_CON_ERROR;
	else if (ret == 0)
		return SELECON_CON_TIMEOUT;
	for (size_t i = 0; i < count; ++i) {
		if (pollfd[i].revents & POLLHUP) {
			// participant disconnected
			*ready_index = i;
			return SELECON_CON_HANGUP;
		} else if (pollfd[i].revents & POLLIN) {
			*ready_index = i;
			return sconn_recv(cons[i], msg);
		}
	}
	return SELECON_CON_ERROR;
}
