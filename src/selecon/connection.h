#pragma once

#include "error.h"

struct SEndpoint;
struct SConnection;
struct SMessage;

// allocates listener for incoming connections on given endpoint
enum SError sconn_listen(struct SConnection **con, struct SEndpoint *ep);

// blocks caller until either connection accepted or timed out
enum SError sconn_accept(struct SConnection *con, struct SConnection **out_con, int timeout_ms);

// allocates and opens connection to given endpoint
enum SError sconn_connect(struct SConnection **con, struct SEndpoint *ep);

// closes connection and frees all allocated resources
enum SError sconn_disconnect(struct SConnection **con);

enum SError sconn_send(struct SConnection *con, struct SMessage *msg);

// allocates new message if ariving message does not fit into provided one.
// Does not free message upon errors
enum SError sconn_recv(struct SConnection *con, struct SMessage **msg);
