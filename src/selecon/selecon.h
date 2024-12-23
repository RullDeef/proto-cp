#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "error.h"
#include "message.h"
#include "role.h"
#include "stypes.h"
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

// creates empty context for further initialization
struct SContext *selecon_context_alloc(void);

// cleans up internal state (exits conference, if in any)
void selecon_context_free(struct SContext **context);

// sets up context params.
//
// By default selecon starts separate thread listening
// on given endpoint for incoming invites and accepts
// those for which invite handler returns true
//
// ep - current participant endpoint address, if NULL - default used
// invite_handler - if NULL accepts any
enum SError selecon_context_init(struct SContext *context,
                                 struct SEndpoint *ep,
                                 invite_handler_fn_t invite_handler,
                                 media_handler_fn_t media_handler);

// sets up context params
//
// address - current participant endpoint address, if NULL - default used
enum SError selecon_context_init2(struct SContext *context,
                                  const char *address,
                                  invite_handler_fn_t invite_handler,
                                  media_handler_fn_t media_handler);

part_id_t selecon_get_self_id(struct SContext *context);

enum SError selecon_set_username(struct SContext *context, const char *username);

// print current context state for debugging
void selecon_context_dump(FILE *fd, struct SContext *context);

// predefined invite handler that accepts any incoming invite
bool selecon_accept_any(struct SMsgInvite *invite);

// predefined invite handler that rejects any incoming invite
bool selecon_reject_any(struct SMsgInvite *invite);

// sends invitation to given endpoint
enum SError selecon_invite(struct SContext *context, struct SEndpoint *ep);

// sends invitation to given endpoint
enum SError selecon_invite2(struct SContext *context, const char *address);

// tell everybody in conference your intention to leave and disconnect from all of them
enum SError selecon_leave_conference(struct SContext *context);

// 48kHz 16bit mono audio signal supported for now (opus codec)
enum SError selecon_stream_alloc_audio(struct SContext *context, sstream_id_t *stream_id);

enum SError selecon_stream_alloc_video(struct SContext *context, sstream_id_t *stream_id);

// closes stream
void selecon_stream_free(struct SContext *context, sstream_id_t *stream_id);

// audio frame must be 20/40/80 ms long at 48kHz mono (960/1920/3840 samples).
// video frame must conform to selected stream resolution
enum SError selecon_stream_push_frame(struct SContext *context,
                                      sstream_id_t stream_id,
                                      struct AVFrame **frame);

#ifdef __cplusplus
}  // extern "C"
#endif
