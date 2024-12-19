#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "dump.h"
#include "selecon.h"
#include "stub.h"

#define STARTS_WITH(cmd, subst) (strncmp(cmd, subst, sizeof(subst) - 1) == 0)

static const char* help_message =
    "%s [OPTIONS]\n"
    "\n"
    "  example demo application for selecon protocol\n"
    "\n"
    "OPTIONS:\n"
    "  -h|--help              show this message\n"
    "  -l|--listen-on address current participant address (default "
    "0.0.0.0:" SELECON_DEFAULT_LISTEN_PORT_STR ")\n"
    "  -u|--user username     set user name\n"
    "  --version              print version and exit\n"
    "  --stub filename        stream given media file in a loop\n"
    "\n"
    "DESCRIPTION:\n"
    "  Address can be IPv4/IPv6 (eg: 192.168.100.1:" SELECON_DEFAULT_LISTEN_PORT_STR
    ") or socket file path\n"
    "  in format file://<os-path-to-socket>.\n";

static const char* participant_address = NULL;
static const char* username            = NULL;
static struct SContext* context        = NULL;
static struct Stub* stub               = NULL;

static struct PacketDump* self_dumper = NULL;

FILE* fp_self = NULL;

static void media_handler(void *user_data, part_id_t part_id, struct AVFrame *frame) {
	printf("media frame recvd from part %llu\n", part_id);
  fwrite(frame->data[0], av_samples_get_buffer_size(NULL, 1, frame->nb_samples, frame->format, 1), 1, fp_self);
  pdump_dump(self_dumper, AVMEDIA_TYPE_AUDIO, frame);
  //av_frame_unref(frame);
}

static void process_help_cmd(char* cmd) {
  char* subcmd = strchr(cmd, ' ');
  if (subcmd == NULL) {
    printf(
      "list of available commands:\n"
      "  dump    print info about current selecon context state\n"
      "  exit    end active conference and close cli tool\n"
      "  help    show this message\n"
      "  invite  send invitation for joining active conference to other client\n"
      "  leave   exit conference without exiting cli tool\n"
      "  quit    same as exit\n"
      "  sleep   sleep\n"
      "  stub    set stub media file for playing in conference\n"
      "\n"
    );
  } else {
    subcmd++;
    if (strcmp(subcmd, "dump") == 0) {
      printf(
        "  > dump\n"
        "\n"
        "  Shows state of active selecon context\n"
        "\n"
      );
    } else if (strcmp(subcmd, "exit") == 0) {
      printf(
        "  > exit\n"
        "\n"
        "  Exit current conference and cli tool\n"
        "\n"
      );
    } else if (strcmp(subcmd, "help") == 0) {
      printf(
        "  > help [{command}]\n"
        "\n"
        "  List available commands or show help about specific command\n"
        "\n"
      );
    } else if (strcmp(subcmd, "invite") == 0) {
      printf(
        "  > invite {ip}\n"
        "  > invite {ip}:{port}\n"
        "  > invite file://{socket-path}\n"
        "\n"
        "  Send invitation to other client\n"
        "\n"
      );
    } else if (strcmp(subcmd, "leave") == 0) {
      printf(
        "  > leave\n"
        "\n"
        "  Leave current conference without closing cli tool\n"
        "\n"
      );
    } else if (strcmp(subcmd, "quit") == 0) {
      printf(
        "  > quit\n"
        "\n"
        "  Exit current conference and cli tool\n"
        "\n"
      );
    } else if (strcmp(subcmd, "sleep") == 0) {
      printf(
        "  > sleep {seconds}\n"
        "\n"
        "  Sleep given amount of seconds\n"
        "\n"
      );
    } else if (strcmp(subcmd, "stub") == 0) {
      printf(
        "  > stub {media-file}\n"
        "  > stub off\n"
        "\n"
        "  Set media file as stub audio/video for showing in conference\n"
        "\n"
      );
    } else {
      printf("unknown command '%s'\n", subcmd);
    }
  }
}

static int process_invite_cmd(char* cmd) {
	char* addr = strchr(cmd, ' ');
	if (addr == NULL) {
		printf("usage: invite <address>\n");
		return 0;
	}
	enum SError err = selecon_invite2(context, ++addr);
	if (err != SELECON_OK)
		printf("failed invite: %s\n", serror_str(err));
	else
		printf("invited participant!\n");
	return 0;
}

static int process_leave_cmd(char* cmd) {
	enum SError err = selecon_leave_conference(context);
	if (err != SELECON_OK)
		printf("failed to leave conference: %s\n", serror_str(err));
	else
		printf("leaving conference\n");
	return 0;
}

static int process_dump_cmd(char* cmd) {
	selecon_context_dump(stdout, context);
	return 0;
}

static int process_sleep_cmd(char* cmd) {
  char* seconds_str = strchr(cmd, ' ');
  if (seconds_str == NULL) {
    printf("usage: sleep {seconds}\n");
    return 0;
  }
  ++seconds_str;
  int seconds = atoi(seconds_str);
  sleep(seconds);
  return 0;
}

static void update_stub(char* option) {
  if (strcmp(option, "off") == 0)
		stub_close(&stub);
	else if (access(option, F_OK) != 0)
		printf("failed to access file %s\n", option);
	else {
    printf("loading file \"%s\"\n", option);
    stub_close(&stub);
    stub = stub_create_dynamic(context, option);
  }
}

static int process_stub_cmd(char* cmd) {
	char* media_file = strchr(cmd, ' ');
	if (media_file == NULL)
		printf("usage: stub <media_file>\n   or: stub off\n");
  else
    update_stub(++media_file);
	return 0;
}

static int cmd_loop(void) {
	context         = selecon_context_alloc();
	enum SError err = selecon_context_init2(context, participant_address, NULL, media_handler);
	if (err != SELECON_OK) {
		printf("failed to initialize context: err = %s\n", serror_str(err));
		return -1;
	}
  if (username != NULL) {
    err = selecon_set_username(context, username);
    if (err != SELECON_OK) {
      printf("failed to set username: err = %s\n", serror_str(err));
      return -1;
    }
  }
	int ret = 0;
	char cmd[1024];
	while (true) {
		printf("> ");
		if (fgets(cmd, sizeof(cmd), stdin) == NULL)
			break;
		char* newline = strchr(cmd, '\n');
		if (newline != NULL)
			*newline = '\0';
		if (STARTS_WITH(cmd, "exit") || STARTS_WITH(cmd, "quit")) {
			break;
    } else if (STARTS_WITH(cmd, "help") || STARTS_WITH(cmd, "?")) {
      process_help_cmd(cmd);
		} else if (STARTS_WITH(cmd, "dump")) {
			if ((ret = process_dump_cmd(cmd)))
				break;
		} else if (STARTS_WITH(cmd, "invite")) {
			if ((ret = process_invite_cmd(cmd)))
				break;
		} else if (STARTS_WITH(cmd, "leave")) {
			if ((ret = process_leave_cmd(cmd)))
				break;
    } else if (STARTS_WITH(cmd, "sleep")) {
      if ((ret = process_sleep_cmd(cmd)))
        break;
		} else if (STARTS_WITH(cmd, "stub")) {
			if ((ret = process_stub_cmd(cmd)))
				break;
		}
	}
	selecon_context_free(&context);
	return ret;
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf(help_message, argv[0]);
			return 0;
		} else if (strcmp(argv[i], "--version") == 0) {
			printf(SELECON_VERSION_STRING "\n");
			return 0;
		} else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--listen-on") == 0) {
			participant_address = argv[++i];
    } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--user") == 0) {
      username = argv[++i];
    } else if (strcmp(argv[i], "--stub") == 0) {
      update_stub(argv[++i]);
		} else {
			printf("unknown option: %s\n", argv[i]);
			return -1;
		}
	}
  fp_self = fopen("self_raw.raw", "wb");
	// init self dumper
	self_dumper = pdump_create("self.mkv");
	int ret     = cmd_loop();
	pdump_free(&self_dumper);
  fclose(fp_self);
	return ret;
}
