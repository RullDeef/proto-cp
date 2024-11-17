#include "selecon.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "connection.h"
#include "endpoint.h"
#include "error.h"
#include "message.h"
#include "participant.h"

struct SContext {
	bool initialized;

	// array of connections to all other conference participants.
	struct SParticipant *participants;

	// number of elements in participants array plus one (self)
	size_t nb_participants;

	// connection for self is NULL
	struct SParticipant self;

	// listener thread for incoming invitations
	pthread_t listener_thread;
	struct SEndpoint listen_ep;
	invite_handler_fn_t invite_handler;
};

struct SContext *selecon_context_alloc(void) {
	return calloc(1, sizeof(struct SContext));
}

void scontext_destroy(struct SContext *context) {
	if (context->initialized) {
		context->initialized = false;
		pthread_join(context->listener_thread, NULL);
		for (int i = 0; i < context->nb_participants; ++i) spart_destroy(&context->participants[i]);
		free(context->participants);
		spart_destroy(&context->self);
	}
}

void selecon_context_free(struct SContext **context) {
	if (*context) {
		scontext_destroy(*context);
		free(*context);
		*context = NULL;
	}
}

static enum SError do_handshake_srv(struct SConnection *con,
                                    bool *accepted,
                                    invite_handler_fn_t handler) {
	struct SMessage *msg = NULL;
	enum SError err      = sconn_recv(con, &msg);
	if (err != SELECON_OK)
		return err;
	struct SInvite invite;
	err = sinvite_parse_message(msg, &invite);
	if (err != SELECON_OK)
		goto fail;
	*accepted = handler(&invite);
	message_free(&msg);
	msg       = message_alloc(sizeof(struct SMessage));
	msg->type = *accepted ? SMSG_INVITE_ACCEPT : SMSG_INVITE_REJECT;
	err       = sconn_send(con, msg);
fail:
	message_free(&msg);
	return err;
}

static enum SError do_handshake_client(struct SConnection *con,
                                       bool *accepted,
                                       struct SInvite *invite) {
	struct SMessage *msg = sinvite_alloc_message(invite);
	enum SError err      = sconn_send(con, msg);
	if (err == SELECON_OK) {
		err = sconn_recv(con, &msg);
		if (err == SELECON_OK)
			*accepted = msg->type == SMSG_INVITE_ACCEPT;
	}
	message_free(&msg);
	return err;
}

// save participant to context
static void join_as_participant(struct SContext *ctx, struct SConnection *con) {
	// ask participant about conference we are joining into.
	//
	// for that purpose we need to define new message type:
	// PART_UPD - send information about conference.
	// If receiver of this message detects incompatability between his
	// participants list and receved - it will propagate changes to other participants.
	// TODO: better reallocs
	ctx->participants =
	    realloc(ctx->participants, ctx->nb_participants * sizeof(struct SParticipant));
	ctx->participants[ctx->nb_participants - 1].id         = 0;  // todo
	ctx->participants[ctx->nb_participants - 1].name       = strdup("new part");
	ctx->participants[ctx->nb_participants - 1].connection = con;
	ctx->nb_participants++;
	printf("joined to others' conference!\n");
}

static void *listener_func(void *arg) {
	struct SContext *ctx = arg;
	struct SConnection *listen_con;
	enum SError err = sconn_listen(&listen_con, &ctx->listen_ep);
	while (ctx->initialized && (err == SELECON_OK || err == SELECON_CON_TIMEOUT)) {
		struct SConnection *part_con;
		err = sconn_accept(listen_con, &part_con, 5000);
		if (err == SELECON_OK) {
			bool accepted = false;
			err           = do_handshake_srv(part_con, &accepted, ctx->invite_handler);
			if (accepted)
				join_as_participant(ctx, part_con);
			else
				sconn_disconnect(&part_con);
		}
	}
	if (err != SELECON_OK)
		printf("listener error: %s\n", serror_str(err));
	return NULL;
}

enum SError selecon_context_init(struct SContext *ctx,
                                 struct SEndpoint *ep,
                                 invite_handler_fn_t invite_handler) {
	if (ctx == NULL)
		return SELECON_INVALID_ARG;
	if (ctx->initialized)
		return SELECON_ALREADY_INIT;
	struct SEndpoint default_ep;
	if (ep == NULL) {
		selecon_parse_endpoint(
		    &default_ep, SELECON_DEFAULT_LISTEN_ADDR, SELECON_DEFAULT_LISTEN_PORT);
		ep = &default_ep;
	}

	ctx->self            = spart_init(SELECON_DEFAULT_PART_NAME);
	ctx->participants    = NULL;
	ctx->nb_participants = 1;
	ctx->initialized     = true;
	ctx->listen_ep       = *ep;
	ctx->invite_handler  = invite_handler == NULL ? selecon_accept_any : invite_handler;
	int perr             = pthread_create(&ctx->listener_thread, NULL, listener_func, ctx);
	if (perr != 0)
		return SELECON_PTHREAD_ERROR;

	enum SError err = SELECON_OK;
	if (err != SELECON_OK)
		scontext_destroy(ctx);
	return err;
}

enum SError selecon_context_init2(struct SContext *context,
                                  const char *address,
                                  invite_handler_fn_t invite_handler) {
	if (address == NULL) {
		return selecon_context_init(context, NULL, invite_handler);
	} else {
		struct SEndpoint ep;
		enum SError err = selecon_parse_endpoint2(&ep, address);
		if (err != SELECON_OK)
			return err;
		return selecon_context_init(context, &ep, invite_handler);
	}
}

void selecon_context_dump(FILE *fd, struct SContext *context) {
	if (context == NULL) {
		fprintf(fd, "(null)");
		return;
	}
	fprintf(fd, "listen_ep: ");
	selecon_endpoint_dump(fd, &context->listen_ep);
	fprintf(fd, "\nnb_participants: %zu\n", context->nb_participants);
	fprintf(fd, "self.id: %llu\n", context->self.id);
	for (int i = 0; i < context->nb_participants - 1; ++i)
		fprintf(fd, "  - %s\n", context->participants[i].name);
}

bool selecon_accept_any(struct SInvite *invite) {
	return true;
}

enum SError selecon_invite(struct SContext *context, struct SEndpoint *ep, struct SInvite *invite) {
	if (context == NULL || ep == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	struct SInvite default_invite;
	if (invite == NULL) {
		sinvite_fill_defaults(&default_invite);
		invite = &default_invite;
	}
	struct SConnection *con;
	enum SError err = sconn_connect(&con, ep);
	if (err != SELECON_OK)
		return err;
	bool accepted = false;
	err           = do_handshake_client(con, &accepted, invite);
	if (!accepted) {
		sconn_disconnect(&con);
		return err;
	}
	// add participant to context
	// TODO: better reallocs
	context->participants =
	    realloc(context->participants, context->nb_participants * sizeof(struct SParticipant));
	context->participants[context->nb_participants - 1].id =
	    0;  // get participant params from invite accept message
	context->participants[context->nb_participants - 1].connection = con;
	context->participants[context->nb_participants - 1].name       = strdup("new part");
	context->nb_participants++;
	printf("participant invited!\n");
	return SELECON_OK;
}

enum SError selecon_invite2(struct SContext *context,
                            const char *address,
                            struct SInvite *invite_params) {
	struct SEndpoint ep;
	enum SError err = selecon_parse_endpoint2(&ep, address);
	if (err != SELECON_OK)
		return err;
	return selecon_invite(context, &ep, invite_params);
}

enum SError selecon_accept_invite(struct SContext *context, const char *address) {
	if (context == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;

	return SELECON_OK;
	// enum SError err;
	// struct SEndpoint ep;
	// struct SConnection *con;

	// err = selecon_parse_endpoint2(&ep, address);
	// if (err != SELECON_OK)
	//   return err;

	// err = sconn_connect(&con, &ep);
	// if (err != SELECON_OK)
	//   return err;

	// return err;
}

// enters conference and starts sending and receiving media content
enum SError selecon_enter_conference(struct SContext *context);

// stops content sharing
enum SError selecon_leave_conference(struct SContext *context);

// sends audio frame to other participants
enum SError selecon_send_audio_frame(struct SContext *context, struct SFrame *frame);

// sends video frame to other participants
enum SError selecon_send_video_frame(struct SContext *context, struct SFrame *frame);
enum SError selecon_enter_conference(struct SContext *context) {
	return SELECON_OK;
}

enum SError selecon_leave_conference(struct SContext *context) {
	return SELECON_OK;
}
