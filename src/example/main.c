#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "selecon.h"

static const char *help_message =
    "%s [OPTIONS]\n"
    "\n"
    "  example demo application for selecon protocol\n"
    "\n"
    "OPTIONS:\n"
    "  -h|--help              show this message\n"
    "  --version              print version and exit\n"
    "  -f|--file filename     stream given video filename in a loop\n"
    "  --invite address       sends invitation on given address (IPv4/IPv6\n"
    "                         accepted, eg: 1.2.3.4:12345)\n"
    "  --invite-timeout T     invitation timeout T in seconds [default: 60]\n"
    "  --wait-invite address  await invitation on given address (set to 'any'\n"
    "                         for listening all interfaces)\n";

static const char *filename = NULL;
static const char *invite_address = NULL;
static int invite_timeout = 60;
static const char *wait_invite_address = NULL;

int run_inviter(const char *address, int timeout_ms, const char *filename);
int run_invitee(const char *address, const char *filename);

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf(help_message, argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--version") == 0) {
      printf(SELECON_VERSION_STRING "\n");
      return 0;
    } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
      filename = argv[++i];
    } else if (strcmp(argv[i], "--invite") == 0) {
      invite_address = argv[++i];
    } else if (strcmp(argv[i], "--invite-timeout") == 0) {
      invite_timeout = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--wait-invite") == 0) {
      invite_address = argv[++i];
    } else {
      printf("unknown option: %s\n", argv[i]);
      return -1;
    }
  }

  if (invite_address && wait_invite_address) {
    printf("options --invite and --wait-invite are mutually exclusive\n");
    return -2;
  }
  if (!invite_address && !wait_invite_address) {
    printf("specify one of --invite or --wait-invite to start conference!\n");
    return 0;
  }
  if (invite_address && invite_timeout < 1) {
    printf("invalid timeout value '%d'. Expected positive integer\n",
           invite_timeout);
    return -2;
  }

  return invite_address
             ? run_inviter(invite_address, invite_timeout * 1000, filename)
             : run_invitee(wait_invite_address, filename);
}
