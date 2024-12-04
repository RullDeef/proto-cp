#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
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
    "  --version              print version and exit\n"
    "  -l|--listen-on address current participant address (default "
    "0.0.0.0:" SELECON_DEFAULT_LISTEN_PORT_STR
    ")\n"
    //    "  -f|--file filename     stream given video filename in a loop\n"
    "\n"
    "DESCRIPTION:\n"
    "  Address can be IPv4/IPv6 (eg: 192.168.100.1:" SELECON_DEFAULT_LISTEN_PORT_STR
    ") or socket file path\n"
    "  in format file://<os-path-to-socket>.\n";

static const char* participant_address = NULL;
static struct SContext* context        = NULL;
static struct Stub* stub               = NULL;

static void media_handler(part_id_t part_id, struct AVFrame* frame) {
	printf("media frame recvd from part %llu\n", part_id);
	av_frame_free(&frame);
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

static int process_stub_cmd(char* cmd) {
	char* media_file = strchr(cmd, ' ');
	if (media_file == NULL) {
		printf(
		    "usage: stub <media_file>\n"
		    "   or: stub off\n");
		return 0;
	}
	++media_file;
	if (strcmp(media_file, "off") == 0) {
		stub_close(&stub);
		return 0;
	}
	if (access(media_file, F_OK) != 0) {
		printf("failed to access file %s\n", media_file);
		return 0;
	}
	printf("loading file \"%s\"\n", media_file);
	stub_close(&stub);
	stub = stub_create_dynamic(context, media_file);
	return 0;
}

static int cmd_loop(void) {
	context         = selecon_context_alloc();
	enum SError err = selecon_context_init2(context, participant_address, NULL, media_handler);
	if (err != SELECON_OK) {
		printf("failed to initialize context: err = %s", serror_str(err));
		return -1;
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
		} else if (STARTS_WITH(cmd, "dump")) {
			if ((ret = process_dump_cmd(cmd)))
				break;
		} else if (STARTS_WITH(cmd, "invite")) {
			if ((ret = process_invite_cmd(cmd)))
				break;
		} else if (STARTS_WITH(cmd, "leave")) {
			if ((ret = process_leave_cmd(cmd)))
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
		} else {
			printf("unknown option: %s\n", argv[i]);
			return -1;
		}
	}
	return cmd_loop();
}
