#pragma once

#include <stddef.h>
#include <stdio.h>

#include "error.h"

struct SEndpoint;
struct SConnection;
struct SMessage;

// debug output of connection state
void sconn_dump(FILE *fd, struct SConnection *con);

// allocates listener for incoming connections on given endpoint
enum SError sconn_listen(struct SConnection **con, struct SEndpoint *ep);

// blocks caller until either connection accepted or timed out
enum SError sconn_accept(struct SConnection *con, struct SConnection **out_con, int timeout_ms);

// blocks caller until either connection accepted or timed out. Accept connection with forceful SSL
// setup. Other side must call sconn_connect_secure
enum SError sconn_accept_secure(struct SConnection *con,
                                struct SConnection **out_con,
                                int timeout_ms);

// allocates and opens connection to given endpoint
enum SError sconn_connect(struct SConnection **con, struct SEndpoint *ep);

// allocates and opens connection to given endpoint. Requires SSL setup on other side as well
enum SError sconn_connect_secure(struct SConnection **con, struct SEndpoint *ep);

// closes connection and frees all allocated resources
enum SError sconn_disconnect(struct SConnection **con);

// does not free message upon errors
enum SError sconn_send(struct SConnection *con, struct SMessage *msg);

// allocates new message if ariving message does not fit into provided one.
// Does not free message upon errors
enum SError sconn_recv(struct SConnection *con, struct SMessage **msg);

// waits on many connections for incoming messages
enum SError sconn_recv_one(struct SConnection **cons,
                           size_t count,
                           struct SMessage **msg,
                           size_t *ready_index,
                           int timeout_ms);
