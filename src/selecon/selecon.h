#pragma once

#include "error.h"
#include "version.h"

struct SEndpoint;

// Main struct for holding conference state
struct SContext {
  // array of endpoint addresses of all conference participants
  struct SEndpoint *endpoints;

  // number of endpoint addresses in endpoints array
  unsigned int nb_endpoints;
};

// creates empty context for further initialization
struct SContext *selecon_context_alloc(void);

// cleans up internal state
void selecon_context_free(struct SContext **context);

// sends invitation by ip. After invitation being accepted contexts of this and
// other clients are linked
//
// returns 0 on success
enum SError selecon_invite_by_ip(struct SContext *context, const char *addr,
                                 unsigned int timeout_ms);

// waits for incoming invitation. Context must be initialized prior calling this
// function. After successfully receiving invitation user must call
// selecon_enter function to actually start transfering media content
//
// This function variant listens both IPv4 and IPv6 broadcast interfaces
//
// returns 0 on success
enum SError selecon_wait_invite(struct SContext *context);

// this version waits on specified address only
enum SError selecon_wait_invite2(struct SContext *context, const char *address);

// enters conference and starts sending and receiving media content
enum SError selecon_enter_conference(struct SContext *context);

// stops content sharing
enum SError selecon_leave_conference(struct SContext *context);
