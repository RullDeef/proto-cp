#pragma once

#include <stdbool.h>
#include <libavutil/frame.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long conf_id_t;
typedef unsigned long long part_id_t;

typedef unsigned long long timestamp_t;

// participant endpoint abstraction (IP + port)
struct SEndpoint;

// invitation options
struct SInvite;

// data stream for passing between participants
struct SStream;

// identifier for accessing stream by client
typedef struct SStream* sstream_id_t;

// Main struct for holding conference state
struct SContext;

struct SMsgInvite;

// invite handing callback. Returns bool weither or not
// invite was accepted.
typedef bool (*invite_handler_fn_t)(struct SMsgInvite *invite);

// recvd media data handling callback. Must initiate
// defered work and quit as soon as possible
typedef void (*media_handler_fn_t)(part_id_t part_id, struct AVFrame *frame);

#ifdef __cplusplus
}  // extern "C"
#endif
