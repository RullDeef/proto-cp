#include "endpoint.h"
#include "error.h"
#include "selecon.h"
#include <stdio.h>
#include <unistd.h>

int run_inviter(char *address, int timeout_ms, const char *filename) {
  enum SError err = SELECON_OK;

  struct SContext *context = selecon_context_alloc();

  struct SEndpoint endpoint;
  err = selecon_parse_endpoint2(&endpoint, address);
  if (err != SELECON_OK) {
    perror("selecon_parse_endpoint2");
    goto cleanup;
  }

  struct SInvite invite;
  invite.timeout = timeout_ms;
  err = selecon_invite(context, &endpoint, &invite);
  if (err != SELECON_OK) {
    perror("selecon_invite");
    goto cleanup;
  }

  err = selecon_enter_conference(context);
  if (err != SELECON_OK) {
    perror("selecon_enter_conference");
    goto cleanup;
  }
  printf("entered conference\n");
  sleep(10);
  err = selecon_leave_conference(context);
  if (err != SELECON_OK) {
    perror("selecon_leave_conference");
    goto cleanup;
  }
  printf("leaved conference\n");

cleanup:
  selecon_context_free(&context);
  return err;
}
