#include "error.h"
#include "selecon.h"
#include <stdio.h>
#include <unistd.h>

int run_inviter(char *address, int timeout_ms, const char *filename) {
  enum SError err = SELECON_OK;

  struct SContext *context = selecon_context_alloc();

  // char *colon = strrchr(address, ':');
  // int port = atoi(colon + 1);
  // *colon = '\0';
  // int af = strchr(address, ':') == NULL ? AF_INET : AF_INET6;
  // inet_pton(af, address, );

  err = selecon_invite_by_ip(context, address, timeout_ms);
  if (err != SELECON_OK) {
    perror("selecon_invite_by_ip");
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
