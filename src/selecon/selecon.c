#define _GNU_SOURCE

#include "selecon.h"

#include <assert.h>
#include <libavcodec/packet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avutility.h"
#include "config.h"
#include "connection.h"
#include "context.h"
#include "debugging.h"
#include "endpoint.h"
#include "error.h"
#include "message.h"
#include "participant.h"
#include "stime.h"
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
		pthread_rwlock_destroy(&context->part_rwlock);
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

// ctx->part_rwlock must be in writer locked state upon calling this function
static void add_participant(struct SContext *ctx,
                            part_id_t id,
                            const char *name,
                            struct SConnection *con) {
	ctx->participants =
	    reallocarray(ctx->participants, ctx->nb_participants, sizeof(struct SParticipant));
	size_t index                        = ctx->nb_participants - 1;
	ctx->participants[index].id         = id;
	ctx->participants[index].connection = con;
	ctx->participants[index].name       = strdup(name);
	ctx->nb_participants++;
}

static void remove_participant(struct SContext *ctx, size_t index) {
	// remove all asociated streams
	scont_close_streams(&ctx->streams, ctx->participants[index].id);
	pthread_rwlock_wrlock(&ctx->part_rwlock);
	spart_destroy(&ctx->participants[index]);
	for (size_t i = index + 1; i < ctx->nb_participants - 1; ++i)
		ctx->participants[i - 1] = ctx->participants[i];
	ctx->nb_participants--;
	pthread_rwlock_unlock(&ctx->part_rwlock);
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

static void handle_text_message(struct SContext *ctx, struct SMsgText *msg) {
	if (ctx->text_handler != NULL)
		ctx->text_handler(ctx, msg->source_part_id, msg->data);
}

static void handle_audio_packet_message(struct SContext *ctx, struct SMsgAudio *msg) {
	sstream_id_t stream =
	    scont_find_stream(&ctx->streams, msg->source_part_id, SSTREAM_AUDIO, SSTREAM_INPUT);
	if (stream == NULL)
		fprintf(stderr,
		        "stream not found for arrived audio packet from part_id = %llu\n",
		        msg->source_part_id);
	else {
		struct AVPacket *packet = av_packet_deserialize(msg->data);
		if (packet == NULL)
			fprintf(stderr, "failed to deserialize packet\n");
		else {
			enum SError err = scont_push_packet(&ctx->streams, stream, &packet);
			if (err != SELECON_OK) {
				fprintf(
				    stderr, "failed to push audio packet to stream: err = %s\n", serror_str(err));
				av_packet_free(&packet);
			}
		}
	}
}

static void handle_video_packet_message(struct SContext *ctx, struct SMsgVideo *msg) {
	sstream_id_t stream =
	    scont_find_stream(&ctx->streams, msg->source_part_id, SSTREAM_VIDEO, SSTREAM_INPUT);
	if (stream == NULL)
		fprintf(stderr,
		        "stream not found for arrived video packet from part_id = %llu\n",
		        msg->source_part_id);
	else {
		struct AVPacket *packet = av_packet_deserialize(msg->data);
		if (packet == NULL)
			fprintf(stderr, "failed to deserialize packet\n");
		else
			scont_push_packet(&ctx->streams, stream, &packet);
	}
}

// general message handler routine
static void handle_message(struct SContext *ctx, size_t part_index, struct SMessage *msg) {
	switch (msg->type) {
		case SMSG_PART_PRESENCE:
			return handle_part_presence_message(ctx, part_index, (struct SMsgPartPresence *)msg);
		case SMSG_TEXT: return handle_text_message(ctx, (struct SMsgText *)msg);
		case SMSG_AUDIO: return handle_audio_packet_message(ctx, (struct SMsgAudio *)msg);
		case SMSG_VIDEO: return handle_video_packet_message(ctx, (struct SMsgVideo *)msg);
		default: printf("unknown message type received: %d\n", msg->type);
	}
}

// received packet from self output stream
static void packet_handler(void *ctx_raw, struct SStream *stream, struct AVPacket *packet) {
	struct SContext *ctx = ctx_raw;
	// send packet to all other participants in conference
	struct SMessage *msg = NULL;
	switch (stream->type) {
		case SSTREAM_AUDIO: msg = message_audio_alloc(ctx->self.id, packet); break;
		case SSTREAM_VIDEO: msg = message_video_alloc(ctx->self.id, packet); break;
		default: fprintf(stderr, "unrecognized stream type: %d\n", stream->type); break;
	}
	pthread_rwlock_rdlock(&ctx->part_rwlock);
	for (int i = 0; i < ctx->nb_participants - 1; ++i) {
		enum SError err = sconn_send(ctx->participants[i].connection, msg);
		if (err != SELECON_OK)
			fprintf(
			    stderr, "failed to send media message to participant: err = %s\n", serror_str(err));
	}
	pthread_rwlock_unlock(&ctx->part_rwlock);
	message_free(&msg);
}

static void *conf_worker(void *arg) {
	struct SContext *ctx     = arg;
	ctx->conf_thread_working = true;
	pthread_rwlock_rdlock(&ctx->part_rwlock);
	size_t cons_count         = ctx->nb_participants - 1;
	struct SConnection **cons = calloc(cons_count, sizeof(struct SConnection *));
	for (size_t i = 0; i < cons_count; ++i) cons[i] = ctx->participants[i].connection;
	pthread_rwlock_unlock(&ctx->part_rwlock);
	struct SMessage *msg = NULL;
	size_t index         = 0;
	enum SError err      = SELECON_OK;
	printf("started conf worker!\n");
	while (ctx->initialized && ctx->nb_participants > 1 &&
	       (err == SELECON_OK || err == SELECON_CON_TIMEOUT)) {
		if (cons_count != ctx->nb_participants - 1) {
			pthread_rwlock_rdlock(&ctx->part_rwlock);
			cons_count = ctx->nb_participants - 1;
			cons       = reallocarray(cons, cons_count, sizeof(struct SConnection *));
			for (size_t i = 0; i < cons_count; ++i) cons[i] = ctx->participants[i].connection;
			pthread_rwlock_unlock(&ctx->part_rwlock);
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
					ctx->conf_id       = invite->conf_id;
					ctx->conf_start_ts = invite->conf_start_ts;
				}
				pthread_rwlock_wrlock(&ctx->part_rwlock);
				add_participant(ctx, invite->part_id, invite->part_name, part_con);
				sstream_id_t audio_stream = scont_alloc_stream(&ctx->streams,
				                                               invite->part_id,
				                                               ctx->conf_start_ts,
				                                               SSTREAM_AUDIO,
				                                               SSTREAM_INPUT);
				sstream_id_t video_stream = scont_alloc_stream(&ctx->streams,
				                                               invite->part_id,
				                                               ctx->conf_start_ts,
				                                               SSTREAM_VIDEO,
				                                               SSTREAM_INPUT);
				pthread_rwlock_unlock(&ctx->part_rwlock);
				// TODO: memory check
				if (ctx->nb_participants == 2 && !ctx->conf_thread_working) {
					pthread_create(&ctx->conf_thread, NULL, conf_worker, ctx);
					pthread_setname_np(ctx->conf_thread, "conf");
				}
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
                                 text_handler_fn_t text_handler,
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
	ctx->conf_start_ts   = get_curr_timestamp();
	ctx->self            = spart_init(SELECON_DEFAULT_PART_NAME);
	ctx->participants    = NULL;
	ctx->nb_participants = 1;
	int ret              = pthread_rwlock_init(&ctx->part_rwlock, NULL);
	assert(ret == 0);
	ctx->listen_ep           = *ep;
	ctx->invite_handler      = invite_handler == NULL ? selecon_accept_any : invite_handler;
	ctx->text_handler        = text_handler;
	ctx->initialized         = true;
	ctx->conf_thread_working = false;
	if (pthread_create(&ctx->listener_thread, NULL, invite_worker, ctx) != 0) {
		spart_destroy(&ctx->self);
		pthread_rwlock_destroy(&ctx->part_rwlock);
		ctx->initialized = false;
		return SELECON_PTHREAD_ERROR;
	}
	pthread_setname_np(ctx->listener_thread, "listener");
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
                                  text_handler_fn_t text_handler,
                                  media_handler_fn_t media_handler) {
	if (address == NULL)
		return selecon_context_init(context, NULL, invite_handler, text_handler, media_handler);
	struct SEndpoint ep = {0};
	enum SError err     = selecon_parse_endpoint2(&ep, address);
	if (err == SELECON_OK)
		err = selecon_context_init(context, &ep, invite_handler, text_handler, media_handler);
	return err;
}

part_id_t selecon_get_self_id(struct SContext *context) {
	if (!context->initialized)
		return -1;
	return context->self.id;
}

enum SError selecon_set_username(struct SContext *context, const char *username) {
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
	pthread_rwlock_rdlock(&context->part_rwlock);
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
	pthread_rwlock_unlock(&context->part_rwlock);
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
	sstream_id_t audio_stream = scont_alloc_stream(
	    &context->streams, -1, context->conf_start_ts, SSTREAM_AUDIO, SSTREAM_INPUT);
	sstream_id_t video_stream = scont_alloc_stream(
	    &context->streams, -1, context->conf_start_ts, SSTREAM_VIDEO, SSTREAM_INPUT);
	if (audio_stream == NULL || video_stream == NULL) {
		return SELECON_MEMORY_ERROR;
	}
	struct SConnection *con = NULL;
	enum SError err         = sconn_connect(&con, ep);
	if (err != SELECON_OK)
		return err;
	struct SMessage *inviteMsg = message_invite_alloc(
	    context->conf_id, context->conf_start_ts, context->self.id, context->self.name);
	struct SMsgInviteAccept *acceptMsg = NULL;
	err                                = do_handshake_client(con, inviteMsg, &acceptMsg);
	if (err != SELECON_OK || acceptMsg == NULL) {
		sconn_disconnect(&con);
		return err;
	}
	printf("participant invited!\n");
	// send other participants info about invitee
	struct SMsgPartPresence *msg = message_part_presence_alloc(1);
	pthread_rwlock_wrlock(&context->part_rwlock);
	for (size_t i = 0; i < context->nb_participants - 1; ++i) {
		printf("sending part presence msg to %s\n", context->participants[i].name);
		msg->states[0].id    = acceptMsg->id;
		msg->states[0].ep    = acceptMsg->ep;
		msg->states[0].state = PART_JOIN;
		sconn_send(context->participants[i].connection, (struct SMessage *)msg);
	}
	add_participant(context, acceptMsg->id, acceptMsg->name, con);
	pthread_rwlock_unlock(&context->part_rwlock);
	audio_stream->part_id = acceptMsg->id;
	video_stream->part_id = acceptMsg->id;
	message_free((struct SMessage **)&msg);
	message_free((struct SMessage **)&acceptMsg);
	if (context->nb_participants == 2 && !context->conf_thread_working) {
		// start conf worker
		int perr = pthread_create(&context->conf_thread, NULL, conf_worker, context);
		if (perr != 0)
			return SELECON_PTHREAD_ERROR;
		pthread_setname_np(context->conf_thread, "conf");
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
	pthread_rwlock_wrlock(&context->part_rwlock);
	for (size_t i = 0; i < context->nb_participants - 1; ++i)
		sconn_send(context->participants[i].connection, (struct SMessage *)msg);
	for (size_t i = 0; i < context->nb_participants - 1; ++i) {
		scont_close_streams(&context->streams, context->participants[i].id);
		spart_destroy(&context->participants[i]);
	}
	context->nb_participants = 1;
	pthread_rwlock_unlock(&context->part_rwlock);
	message_free((struct SMessage **)&msg);
	context->conf_id       = rand();
	context->conf_start_ts = get_curr_timestamp();
	return SELECON_OK;
}

enum SError selecon_send_text(struct SContext *context, const char *text) {
	if (context == NULL || text == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	struct SMessage *msg = message_text_alloc(context->self.id, text);
	pthread_rwlock_rdlock(&context->part_rwlock);
	for (size_t i = 0; i < context->nb_participants - 1; ++i)
		sconn_send(context->participants[i].connection, msg);
	pthread_rwlock_unlock(&context->part_rwlock);
	message_free(&msg);
	return SELECON_OK;
}

// 48kHz 16bit mono audio signal supported for now (opus codec)
enum SError selecon_stream_alloc_audio(struct SContext *context, sstream_id_t *stream_id) {
	if (context == NULL || stream_id == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	*stream_id = scont_alloc_stream(
	    &context->streams, context->self.id, context->conf_start_ts, SSTREAM_AUDIO, SSTREAM_OUTPUT);
	if (*stream_id == NULL)
		return SELECON_MEMORY_ERROR;
	return SELECON_OK;
}

enum SError selecon_stream_alloc_video(struct SContext *context, sstream_id_t *stream_id) {
	if (context == NULL || stream_id == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	*stream_id = scont_alloc_stream(
	    &context->streams, context->self.id, context->conf_start_ts, SSTREAM_VIDEO, SSTREAM_OUTPUT);
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
                                      struct AVFrame **frame) {
	if (context == NULL || stream_id == NULL || frame == NULL)
		return SELECON_INVALID_ARG;
	if (!context->initialized)
		return SELECON_EMPTY_CONTEXT;
	return scont_push_frame(&context->streams, stream_id, frame);
}
