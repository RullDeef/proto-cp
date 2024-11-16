#include "error.h"
#include "selecon.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int run_invitee(const char *address, const char *filename) {
  struct SContext *context = selecon_context_alloc();

  enum SError err =  selecon_accept_invite(context,
      strcmp(address, "any") == 0 ? NULL: address);
  if (err != SELECON_OK) {
    perror("selecon_wait_invite(2)");
    goto cleanup;
  }

  err = selecon_enter_conference(context);
  if (err != SELECON_OK) {
    perror("selecon_enter_conference");
    goto cleanup;
  }
  printf("entered conference\n");
  sleep(15);
  err = selecon_leave_conference(context);
  if (err != SELECON_OK) {
    perror("selecon_leave_conference\n");
    goto cleanup;
  }

cleanup:
  selecon_context_free(&context);
  return err;
}
