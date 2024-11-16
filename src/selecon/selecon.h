#pragma once

#include "error.h"
#include "invite.h"
#include "role.h"
#include "version.h"
#include <stdio.h>

// participant endpoint abstraction (IP + port)
struct SEndpoint;

// invitation options
// struct SInvite;

// data frame for passing between participants
struct SFrame;

// Main struct for holding conference state
struct SContext;

// creates empty context for further initialization
struct SContext *selecon_context_alloc(void);

// cleans up internal state
void selecon_context_free(struct SContext **context);

// sets up context params
//
// ep - current participant endpoint address, if NULL - default used
enum SError selecon_context_init(struct SContext *context,
                                 struct SEndpoint *ep);

// sets up context params
//
// address - current participant endpoint address, if NULL - default used
enum SError selecon_context_init2(struct SContext *context,
                                  const char* address);

// print current context state for debugging
void selecon_context_dump(FILE* fd, struct SContext* context);

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
enum SError selecon_accept_invite(struct SContext *context,
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
