#include "selecon.h"
#include "error.h"
#include <stdlib.h>

struct SContext *selecon_context_alloc(void) {
  struct SContext *context = malloc(sizeof(struct SContext));
  return context;
}

void selecon_context_free(struct SContext **context) {
  if (*context) {
    free(*context);
    *context = NULL;
  }
}

enum SError selecon_invite_by_ip(struct SContext *context, const char *addr,
                                 unsigned int timeout_ms) {
  return SELECON_OK;
}

enum SError selecon_wait_invite(struct SContext *context) { return SELECON_OK; }

enum SError selecon_wait_invite2(struct SContext *context,
                                 const char *address) {
  return SELECON_OK;
}

enum SError selecon_enter_conference(struct SContext *context) {
  return SELECON_OK;
}

enum SError selecon_leave_conference(struct SContext *context) {
  return SELECON_OK;
}
