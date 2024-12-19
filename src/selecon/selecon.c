#include "selecon.h"

#include <libavcodec/packet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "connection.h"
#include "context.h"
#include "endpoint.h"
#include "error.h"
#include "message.h"
#include "participant.h"
#include "stream.h"

static int init_recursive_mutex(pthread_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	int ret = pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return ret;
}

struct SContext *selecon_context_alloc(void) {
	return calloc(1, sizeof(struct SContext));
}

void scontext_destroy(struct SContext *context) {
	if (context->initialized) {
		context->initialized = false;
		pthread_join(context->listener_thread, NULL);
		if (context->conf_thread_working)
			pthread_join(context->conf_thread, NULL);
		for (size_t i = 0; i < context->nb_participants - 1; ++i)
			spart_destroy(&context->participants[i]);
		free(context->participants);
		spart_destroy(&context->self);
		pthread_mutex_destroy(&context->part_mutex);
		scont_free(&context->streams);
	}
}

void selecon_context_free(struct SContext **context) {
	if (*context) {
		scontext_destroy(*context);
		free(*context);
		*context = NULL;
	}
}

static void add_participant(struct SContext *ctx,
                            part_id_t id,
                            const char *name,
                            struct SConnection *con) {
	pthread_mutex_lock(&ctx->part_mutex);
	ctx->participants =
	    reallocarray(ctx->participants, ctx->nb_participants, sizeof(struct SParticipant));
	size_t index                        = ctx->nb_participants - 1;
	ctx->participants[index].id         = id;
	ctx->participants[index].connection = con;
	ctx->participants[index].name       = strdup(name);
	ctx->nb_participants++;
	pthread_mutex_unlock(&ctx->part_mutex);
}

static void remove_participant(struct SContext *ctx, size_t index) {
	pthread_mutex_lock(&ctx->part_mutex);
	spart_destroy(&ctx->participants[index]);
	for (size_t i = index + 1; i < ctx->nb_participants - 1; ++i)
		ctx->participants[i - 1] = ctx->participants[i];
	ctx->nb_participants--;
	pthread_mutex_unlock(&ctx->part_mutex);
}

// handshake procesdure
//
// | step | client (inviter)  | server (invitee)  |
// | :--- | :---------------- | :---------------- |
// | 1    | sends invite msg  | recvs invite msg  |
// | 2    |                   | decides accept    |
// | 3    | recvs confirm msg | sends confirm msg |
static enum SError do_handshake_srv(struct SParticipant *self,
                                    struct SEndpoint *listen_ep,
                                    struct SConnection *con,
                                    struct SMsgInvite **invite,
                                    invite_handler_fn_t handler) {
	struct SMessage *msg = NULL;
	enum SError err      = sconn_recv(con, &msg);
	if (err != SELECON_OK || msg->type != SMSG_INVITE)
		goto cleanup;
	bool accepted = handler((struct SMsgInvite *)msg);
	if (!accepted) {
		msg->size = sizeof(struct SMessage);
		msg->type = SMSG_INVITE_REJECT;
		err       = sconn_send(con, msg);
		goto cleanup;
	}
	*invite = (struct SMsgInvite *)msg;
	msg     = message_invite_accept_alloc(self->id, self->name, listen_ep);
	err     = sconn_send(con, msg);
cleanup:
	message_free(&msg);
	return err;
}

static enum SError do_handshake_client(struct SConnection *con,
                                       struct SMessage *msg,
                                       struct SMsgInviteAccept **acceptMsg) {
	enum SError err = sconn_send(con, msg);
	if (err != SELECON_OK)
		return err;
	err = sconn_recv(con, &msg);
	if (err == SELECON_OK) {
		if (msg->type == SMSG_INVITE_ACCEPT)
			*acceptMsg = (struct SMsgInviteAccept *)msg;
		else {
			message_free(&msg);
			err = SELECON_INVITE_REJECTED;
		}
	} else
		message_free(&msg);
	return err;
}

static void handle_part_presence_message(struct SContext *ctx,
                                         size_t part_index,
                                         struct SMsgPartPresence *msg) {
	for (size_t i = 0; i < msg->count; ++i) {
		if (msg->states[i].state == PART_JOIN) {
			// meet with newbe using invite
			enum SError err = selecon_invite(ctx, &msg->states[i].ep);
			if (err != SELECON_OK)
				printf("failed to meet participant %llu: %s\n", msg->states[i].id, serror_str(err));
		} else if (msg->states[i].state == PART_LEAVE) {
			// disconnect from participant
			printf("removed participant %llu\n", ctx->participants[part_index].id);
			remove_participant(ctx, part_index);
		}
	}
}

// general message handler routine
static void handle_message(struct SContext *ctx, size_t part_index, struct SMessage *msg) {
	switch (msg->type) {
		case SMSG_PART_PRESENCE:
			handle_part_presence_message(ctx, part_index, (struct SMsgPartPresence *)msg);
			break;
		default: printf("unknown message type received: %d\n", msg->type);
	}
}

// received packet from self output stream
static void packet_handler(void *ctx_raw, struct SStream *stream, struct AVPacket *packet) {
  struct SContext* ctx = ctx_raw;
  // TODO: do packet routing to other participants
  // for now - route back into debug self input stream
  sstream_id_t input_stream = scont_find_stream(&ctx->streams, stream->part_id, stream->type, SSTREAM_INPUT);
  if (input_stream != NULL) {
    enum SError err = scont_push_packet(&ctx->streams, input_stream, packet);
    if (err != SELECON_OK)
      printf("failed to push packet: err = %s\n", serror_str(err));
  } else {
    printf("self debug input stream not found\n");
  	av_packet_unref(packet);
  }
}

static void *conf_worker(void *arg) {
	struct SContext *ctx     = arg;
	ctx->conf_thread_working = true;
	pthread_mutex_lock(&ctx->part_mutex);
	size_t cons_count         = ctx->nb_participants - 1;
	struct SConnection **cons = calloc(cons_count, sizeof(struct SConnection *));
	for (size_t i = 0; i < cons_count; ++i) cons[i] = ctx->participants[i].connection;
	pthread_mutex_unlock(&ctx->part_mutex);
	struct SMessage *msg = NULL;
	size_t index         = 0;
	enum SError err      = SELECON_OK;
	printf("started conf worker!\n");
	while (ctx->initialized && ctx->nb_participants > 1 &&
	       (err == SELECON_OK || err == SELECON_CON_TIMEOUT)) {
		if (cons_count != ctx->nb_participants - 1) {
			pthread_mutex_lock(&ctx->part_mutex);
			cons_count = ctx->nb_participants - 1;
			cons       = reallocarray(cons, cons_count, sizeof(struct SConnection *));
			for (size_t i = 0; i < cons_count; ++i) cons[i] = ctx->participants[i].connection;
			pthread_mutex_unlock(&ctx->part_mutex);
		}
		// recv message from any of participants
		enum SError err = sconn_recv_one(cons, cons_count, &msg, &index, 1000);
		if (err == SELECON_CON_HANGUP) {
			// participant accidently disconnected!
			printf("participant %zu disconnected!\n", index);
			remove_participant(ctx, index);
		}
		if (err == SELECON_OK)
			handle_message(ctx, index, msg);
	}
	ctx->conf_thread_working = false;
	printf("conf worker stopped\n");
	message_free(&msg);
	free(cons);
	return NULL;
}

static void *invite_worker(void *arg) {
	struct SContext *ctx           = arg;
	struct SConnection *listen_con = NULL;
	enum SError err                = sconn_listen(&listen_con, &ctx->listen_ep);
	while (ctx->initialized && (err == SELECON_OK || err == SELECON_CON_TIMEOUT)) {
		struct SConnection *part_con = NULL;
		err                          = sconn_accept(listen_con, &part_con, 5000);
		if (err == SELECON_OK) {
			struct SMsgInvite *invite = NULL;
			err                       = do_handshake_srv(
                &ctx->self, &ctx->listen_ep, part_con, &invite, ctx->invite_handler);
			if (err != SELECON_OK || invite == NULL)
				sconn_disconnect(&part_con);
			else {
#ifndef NDEBUG
				printf("successful handshake con = ");
				sconn_dump(stdout, part_con);
				printf("\n");
#endif
				if (ctx->conf_id != invite->conf_id) {
					selecon_leave_conference(ctx);  // leave old conference
					ctx->conf_id = invite->conf_id;
				}
				add_participant(ctx, invite->part_id, invite->part_name, part_con);
        sstream_id_t audio_stream = scont_alloc_stream(&ctx->streams, invite->part_id, SSTREAM_AUDIO, SSTREAM_INPUT);
        sstream_id_t video_stream = scont_alloc_stream(&ctx->streams, invite->part_id, SSTREAM_VIDEO, SSTREAM_INPUT);
        // TODO: memory check
				if (ctx->nb_participants == 2 && !ctx->conf_thread_working)
					pthread_create(&ctx->conf_thread, NULL, conf_worker, ctx);
				message_free((struct SMessage **)&invite);
			}
		}
	}
	sconn_disconnect(&listen_con);
	if (ctx->initialized && err != SELECON_OK)
		printf("listener error: %s\n", serror_str(err));
	return NULL;
}

enum SError selecon_context_init(struct SContext *ctx,
                                 struct SEndpoint *ep,
                                 invite_handler_fn_t invite_handler,
                                 media_handler_fn_t media_handler) {
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
	srand(time(NULL));
	ctx->conf_id         = rand();
	ctx->self            = spart_init(SELECON_DEFAULT_PART_NAME);
	ctx->participants    = NULL;
	ctx->nb_participants = 1;
	init_recursive_mutex(&ctx->part_mutex);
	ctx->listen_ep           = *ep;
	ctx->invite_handler      = invite_handler == NULL ? selecon_accept_any : invite_handler;
	ctx->initialized         = true;
	ctx->conf_thread_working = false;
	if (pthread_create(&ctx->listener_thread, NULL, invite_worker, ctx) != 0) {
		spart_destroy(&ctx->self);
		pthread_mutex_destroy(&ctx->part_mutex);
		ctx->initialized = false;
		return SELECON_PTHREAD_ERROR;
	}
#ifndef NDEBUG
	printf("init selecon ctx [listens ");
	selecon_endpoint_dump(stdout, ep);
	printf("]\n");
#endif
	scont_init(&ctx->streams, media_handler, packet_handler, ctx);
	return SELECON_OK;
}

enum SError selecon_context_init2(struct SContext *context,
                                  const char *address,
                                  invite_handler_fn_t invite_handler,
                                  media_handler_fn_t media_handler) {
	if (address == NULL)
		return selecon_context_init(context, NULL, invite_handler, media_handler);
	struct SEndpoint ep;
	enum SError err = selecon_parse_endpoint2(&ep, address);
	if (err == SELECON_OK)
		err = selecon_context_init(context, &ep, invite_handler, media_handler);
	return err;
}

enum SError selecon_set_username(struct SContext *context, const char* username) {
  if (username == NULL)
    return SELECON_INVALID_ARG;
  if (!context->initialized)
    return SELECON_EMPTY_CONTEXT;
  spart_rename(&context->self, username);
  return SELECON_OK;
}

void selecon_context_dump(FILE *fd, struct SContext *context) {
	if (context == NULL) {
		fprintf(fd, "(null)");
		return;
	}
	fprintf(fd, "conf_id: %llu\n", context->conf_id);
	fprintf(fd, "listen_ep: ");
	selecon_endpoint_dump(fd, &context->listen_ep);
	fprintf(fd, "\n");
	pthread_mutex_lock(&context->part_mutex);
	fprintf(fd, "nb_participants: %zu\n", context->nb_participants);
	fprintf(fd, "  - ");
	spart_dump(fd, &context->self);
	fprintf(fd, " (self)\n");
	for (int i = 0; i < context->nb_participants - 1; ++i) {
		fprintf(fd, "  - ");
		spart_dump(fd, &context->participants[i]);
		fprintf(fd, "\n");
	}
	scont_dump(fd, &context->streams);
	pthread_mutex_unlock(&context->part_mutex);
}

bool selecon_accept_any(struct SMsgInvite *invite) {
	return true;
}

bool selecon_reject_any(struct SMsgInvite *invite) {
	return false;
}

enum SError selecon_invite(struct SContext *context, struct SEndpoint *ep) {
	if (context == NULL || ep == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
  // create input streams for recving content from new participant
  sstream_id_t audio_stream = scont_alloc_stream(&context->streams, -1, SSTREAM_AUDIO, SSTREAM_INPUT);
  sstream_id_t video_stream = scont_alloc_stream(&context->streams, -1, SSTREAM_VIDEO, SSTREAM_INPUT);
  if (audio_stream == NULL || video_stream == NULL) {
    return SELECON_MEMORY_ERROR;
  }
	struct SConnection *con = NULL;
	enum SError err         = sconn_connect(&con, ep);
	if (err != SELECON_OK)
		return err;
	struct SMessage *inviteMsg =
	    message_invite_alloc(context->conf_id, context->self.id, context->self.name);
	struct SMsgInviteAccept *acceptMsg = NULL;
	err                                = do_handshake_client(con, inviteMsg, &acceptMsg);
	if (err != SELECON_OK || acceptMsg == NULL) {
		sconn_disconnect(&con);
		return err;
	}
	printf("participant invited!\n");
	// send other participants info about invitee
	struct SMsgPartPresence *msg = message_part_presence_alloc(1);
	pthread_mutex_lock(&context->part_mutex);
	for (size_t i = 0; i < context->nb_participants - 1; ++i) {
		printf("sending part presence msg to %s\n", context->participants[i].name);
		msg->states[0].id    = acceptMsg->id;
		msg->states[0].ep    = acceptMsg->ep;
		msg->states[0].state = PART_JOIN;
		sconn_send(context->participants[i].connection, (struct SMessage *)msg);
	}
	add_participant(context, acceptMsg->id, acceptMsg->name, con);
	pthread_mutex_unlock(&context->part_mutex);
  audio_stream->part_id = acceptMsg->id;
  video_stream->part_id = acceptMsg->id;
	message_free((struct SMessage **)&msg);
	message_free((struct SMessage **)&acceptMsg);
	if (context->nb_participants == 2 && !context->conf_thread_working) {
		// start conf worker
		int perr = pthread_create(&context->conf_thread, NULL, conf_worker, context);
		if (perr != 0)
			return SELECON_PTHREAD_ERROR;
	}
	return SELECON_OK;
}

enum SError selecon_invite2(struct SContext *context, const char *address) {
	struct SEndpoint ep;
	enum SError err = selecon_parse_endpoint2(&ep, address);
	if (err != SELECON_OK)
		return err;
	return selecon_invite(context, &ep);
}

enum SError selecon_leave_conference(struct SContext *context) {
	if (context == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	if (context->nb_participants == 1)
		return SELECON_OK;
	struct SMsgPartPresence *msg = message_part_presence_alloc(1);
	msg->states[0].id            = context->self.id;
	msg->states[0].ep            = (struct SEndpoint){};
	msg->states[0].state         = PART_LEAVE;
	pthread_mutex_lock(&context->part_mutex);
	for (size_t i = 0; i < context->nb_participants - 1; ++i)
		sconn_send(context->participants[i].connection, (struct SMessage *)msg);
	for (size_t i = 0; i < context->nb_participants - 1; ++i)
		spart_destroy(&context->participants[i]);
	context->nb_participants = 1;
	pthread_mutex_unlock(&context->part_mutex);
	return SELECON_OK;
}

// 48kHz 16bit mono audio signal supported for now (opus codec)
enum SError selecon_stream_alloc_audio(struct SContext *context, sstream_id_t *stream_id) {
	if (context == NULL || stream_id == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
  // TODO: remove this debug self input stream
  sstream_id_t debug_input_stream =
    scont_alloc_stream(&context->streams, context->self.id, SSTREAM_AUDIO, SSTREAM_INPUT);
  if (debug_input_stream == NULL)
    return SELECON_MEMORY_ERROR;
  *stream_id =
	    scont_alloc_stream(&context->streams, context->self.id, SSTREAM_AUDIO, SSTREAM_OUTPUT);
  if (*stream_id == NULL) {
    scont_close_stream(&context->streams, &debug_input_stream);
    return SELECON_MEMORY_ERROR;
  }
	return SELECON_OK;
}

enum SError selecon_stream_alloc_video(struct SContext *context, sstream_id_t *stream_id) {
	if (context == NULL || stream_id == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	*stream_id =
	    scont_alloc_stream(&context->streams, context->self.id, SSTREAM_VIDEO, SSTREAM_OUTPUT);
	return SELECON_OK;
}

// closes stream
void selecon_stream_free(struct SContext *context, sstream_id_t *stream_id) {
	if (context == NULL || stream_id == NULL || *stream_id == NULL)
		return;
	if (context->initialized)
		scont_close_stream(&context->streams, stream_id);
}

enum SError selecon_stream_push_frame(struct SContext *context,
                                      sstream_id_t stream_id,
                                      struct AVFrame *frame) {
	if (context == NULL || stream_id == NULL || frame == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	return scont_push_frame(&context->streams, stream_id, frame);
}
