#pragma once

#include <libavutil/frame.h>
#include <stdbool.h>

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
typedef struct SStream *sstream_id_t;

// Main struct for holding conference state
struct SContext;

struct SMsgInvite;

// invite handing callback. Returns bool weither or not
// invite was accepted.
typedef bool (*invite_handler_fn_t)(struct SMsgInvite *invite);

// recvd text message handling callback
typedef void (*text_handler_fn_t)(void *user_data,
                                  part_id_t part_id,
                                  const char* text);

// recvd media data handling callback. Must initiate
// defered work and quit as soon as possible
typedef void (*media_handler_fn_t)(void *user_data,
                                   part_id_t part_id,
                                   enum AVMediaType mtype,
                                   struct AVFrame *frame);

struct AVPacket;
typedef void (*packet_handler_fn_t)(void *user_data,
                                    struct SStream *stream,
                                    struct AVPacket *packet);

#ifdef __cplusplus
}  // extern "C"
#endif
