#pragma once

#include "error.h"
#include "invite.h"
#include "role.h"
#include "version.h"
#include <stdbool.h>
#include <stdio.h>

// participant endpoint abstraction (IP + port)
struct SEndpoint;

// invitation options
struct SInvite;

// data frame for passing between participants
struct SFrame;

// Main struct for holding conference state
struct SContext;

// invite handing callback. Returns bool weither or not
// invite was accepted.
typedef bool (*invite_handler_fn_t)(struct SInvite* invite);

// creates empty context for further initialization
struct SContext *selecon_context_alloc(void);

// cleans up internal state
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
                                 invite_handler_fn_t invite_handler);

// sets up context params
//
// address - current participant endpoint address, if NULL - default used
enum SError selecon_context_init2(struct SContext *context,
                                  const char* address,
                                  invite_handler_fn_t invite_handler);

// print current context state for debugging
void selecon_context_dump(FILE* fd, struct SContext* context);

// predefined invite handler that accepts any incoming invite
bool selecon_accept_any(struct SInvite* invite);

// sends invitation to given endpoint
enum SError selecon_invite(struct SContext *context, struct SEndpoint *ep,
                           struct SInvite *invite_params);

// sends invitation to given endpoint
enum SError selecon_invite2(struct SContext *context, const char* address,
                            struct SInvite *invite_params);

// waits for incoming invitation. Context must be initialized prior calling this
// function. After successfully receiving invitation user must call
// selecon_enter function to actually start transfering media content
//
// If address is NULL function listens both IPv4 and IPv6 broadcast interfaces
enum SError selecon_accept_invite_from(struct SContext *context,
                                       const char *address);

// enters conference and starts sending and receiving media content
enum SError selecon_enter_conference(struct SContext *context);

// stops content sharing
enum SError selecon_leave_conference(struct SContext *context);

// sends audio frame to other participants
enum SError selecon_send_audio_frame(struct SContext *context,
                                     struct SFrame *frame);

// sends video frame to other participants
enum SError selecon_send_video_frame(struct SContext *context,
                                     struct SFrame *frame);
