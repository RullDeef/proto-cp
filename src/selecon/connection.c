#include "connection.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <unistd.h>

#include "cert.h"
#include "config.h"
#include "endpoint.h"
#include "message.h"

struct SConnection {
	int fd;
	struct SEndpoint src_ep;
	struct SEndpoint dst_ep;

	// if ssl is NULL - connection is raw and not secured
	SSL *ssl;
	SSL_CTX *ssl_ctx;
};

static atomic_int ssl_usage_counter = 0;

static void ssl_init(void) {
	if (atomic_fetch_add(&ssl_usage_counter, 1) == 0) {
		SSL_load_error_strings();
		OpenSSL_add_all_algorithms();
	}
}

static void ssl_destroy(void) {
	if (atomic_fetch_add(&ssl_usage_counter, -1) == 1) {
		ERR_free_strings();
		EVP_cleanup();
	}
}

static SSL_CTX *ssl_new_server_ctx(void) {
	const char *cert = cert_get_cert_path();
	const char *key  = cert_get_key_path();
	if (cert == NULL || key == NULL)
		return NULL;
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_server_method());
	if (SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) > 0) {
		if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) > 0) {
			return ctx;
		}
	}
	SSL_CTX_free(ctx);
	return NULL;
}

static SSL_CTX *ssl_new_client_ctx(void) {
	SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
	return ctx;
}

void sconn_dump(FILE *fd, struct SConnection *con) {
	if (fd == NULL || con == NULL)
		return;
	fprintf(fd, "{src:");
	selecon_endpoint_dump(fd, &con->src_ep);
	fprintf(fd, ", dst:");
	selecon_endpoint_dump(fd, &con->dst_ep);
	fprintf(fd, "}");
}

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

#ifdef SELECON_USE_SECURE_CONNECTION
enum SError sconn_accept_secure(struct SConnection *con,
                                struct SConnection **out_con,
                                int timeout_ms) {
	enum SError err = sconn_accept(con, out_con, timeout_ms);
	if (err != SELECON_OK)
		return err;
	ssl_init();
	if (((*out_con)->ssl_ctx = ssl_new_server_ctx()) != NULL) {
		(*out_con)->ssl = SSL_new((*out_con)->ssl_ctx);
		SSL_set_fd((*out_con)->ssl, (*out_con)->fd);
		if (SSL_accept((*out_con)->ssl) > 0)
			return SELECON_OK;
	}
	ERR_print_errors_fp(stderr);
	sconn_disconnect(out_con);
	return SELECON_SSL_ERROR;
}
#endif

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

#ifdef SELECON_USE_SECURE_CONNECTION
enum SError sconn_connect_secure(struct SConnection **con, struct SEndpoint *ep) {
	enum SError err = sconn_connect(con, ep);
	if (err != SELECON_OK)
		return err;
	ssl_init();
	if (((*con)->ssl_ctx = ssl_new_client_ctx()) != NULL) {
		(*con)->ssl = SSL_new((*con)->ssl_ctx);
		SSL_set_fd((*con)->ssl, (*con)->fd);
		if (SSL_connect((*con)->ssl) > 0)
			return SELECON_OK;
	}
	ERR_print_errors_fp(stderr);
	sconn_disconnect(con);
	return SELECON_SSL_ERROR;
}
#endif

enum SError sconn_disconnect(struct SConnection **con) {
	if (con == NULL || *con == NULL)
		return SELECON_INVALID_ARG;
#ifdef SELECON_USE_SECURE_CONNECTION
	if ((*con)->ssl_ctx != NULL) {
		if ((*con)->ssl != NULL) {
			SSL_shutdown((*con)->ssl);  // TODO: solve EPIPE here (p2p reject test)
			SSL_free((*con)->ssl);
		}
		SSL_CTX_free((*con)->ssl_ctx);
		ssl_destroy();
	}
#endif
	close((*con)->fd);
	free(*con);
	*con = NULL;
	return SELECON_OK;
}

enum SError sconn_send(struct SConnection *con, struct SMessage *msg) {
	if (con == NULL || msg == NULL)
		return SELECON_INVALID_ARG;
#ifdef SELECON_USE_SECURE_CONNECTION
	if (con->ssl != NULL) {
		int ret = 0;
	ssl_retry:
		ret = SSL_write(con->ssl, msg, msg->size);
		if (ret <= 0) {
			switch (SSL_get_error(con->ssl, ret)) {
				case SSL_ERROR_WANT_READ:
				case SSL_ERROR_WANT_WRITE: goto ssl_retry;
				default: ERR_print_errors_fp(stderr); return SELECON_CON_ERROR;
			}
		}
	} else
#endif
	{
		if (send(con->fd, msg, msg->size, 0) == -1) {
			perror("send");
			return SELECON_CON_ERROR;
		}
	}
	return SELECON_OK;
}

static int sconn_recv_part(struct SConnection *con, void *buffer, size_t size) {
	int ret = 0;
#ifdef SELECON_USE_SECURE_CONNECTION
	if (con->ssl != NULL) {
		ret = SSL_read(con->ssl, buffer, size);
		if (ret == -1)
			ERR_print_errors_fp(stderr);
	} else
#endif
	{
		ret = recv(con->fd, buffer, size, 0);
		if (ret == -1)
			perror("recv");
	}
	return ret;
}

enum SError sconn_recv(struct SConnection *con, struct SMessage **msg) {
	if (con == NULL || msg == NULL)
		return SELECON_INVALID_ARG;
	size_t size = 0;
	int ret     = sconn_recv_part(con, &size, sizeof(size_t));
	if (ret == -1)
		return SELECON_CON_ERROR;
	else if (ret == 0) {
#ifdef SELECON_USE_SECURE_CONNECTION
		if (con->ssl != NULL) {  // close ssl connection if any
			SSL_free(con->ssl);
			con->ssl = NULL;
			SSL_CTX_free(con->ssl_ctx);
			con->ssl_ctx = NULL;
		}
#endif
		return SELECON_CON_HANGUP;
	}
	if (*msg == NULL || (*msg)->size < size) {
		message_free(msg);
		*msg = message_alloc(size);
	}
	ret = sconn_recv_part(con, &(*msg)->type, size - sizeof(size));
	if (ret == -1)
		return SELECON_CON_ERROR;
	else if (ret != size - sizeof(size)) {
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
	bool all_invalid = true;
	for (size_t i = 0; i < count; ++i) all_invalid = all_invalid && cons[i] == NULL;
	if (all_invalid)
		return SELECON_NO_VALID_CONNECTIONS;
	struct pollfd *pollfd = alloca(count * sizeof(struct pollfd));
	for (size_t i = 0; i < count; ++i) {
		pollfd[i].fd      = cons[i] == NULL ? -1 : cons[i]->fd;
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
#ifdef SELECON_USE_SECURE_CONNECTION
			if (cons[i]->ssl != NULL) {
				SSL_free(cons[i]->ssl);
				cons[i]->ssl = NULL;
				SSL_CTX_free(cons[i]->ssl_ctx);
				cons[i]->ssl_ctx = NULL;
			}
#endif
			return SELECON_CON_HANGUP;
		} else if (pollfd[i].revents & POLLIN) {
			*ready_index = i;
			return sconn_recv(cons[i], msg);
		}
	}
	return SELECON_CON_ERROR;
}
