#include "stream.h"

#include <assert.h>
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "participant.h"
#include "stypes.h"

static int init_recursive_mutex(pthread_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	int ret = pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return ret;
}

static void insert_frame(struct SStream *stream, struct AVFrame *frame) {
	assert(stream->dir == SSTREAM_OUTPUT);
	struct SFrame *sframe = calloc(1, sizeof(struct SFrame));
	assert(sframe != NULL);
	sframe->avframe = frame;
	pthread_mutex_lock(&stream->mutex);
	if (stream->tail != NULL)
		stream->tail->next = sframe;
	else {
		stream->queue = sframe;
		pthread_cond_signal(&stream->cond);
	}
	stream->tail = sframe;
	pthread_mutex_unlock(&stream->mutex);
}

static void insert_packet(struct SStream *stream, struct AVPacket *packet) {
	assert(stream->dir == SSTREAM_INPUT);
	struct SFrame *sframe = calloc(1, sizeof(struct SFrame));
	assert(sframe != NULL);
	sframe->avpacket = packet;
	pthread_mutex_lock(&stream->mutex);
	if (stream->tail != NULL)
		stream->tail->next = sframe;
	else {
		stream->queue = sframe;
		pthread_cond_signal(&stream->cond);
	}
	stream->tail = sframe;
	pthread_mutex_unlock(&stream->mutex);
}

static struct AVFrame *pop_frame(struct SStream *stream) {
	assert(stream != NULL);
	pthread_mutex_lock(&stream->mutex);
	if (stream->queue == NULL)
		pthread_cond_wait(&stream->cond, &stream->mutex);
	struct AVFrame *frame = NULL;
	struct SFrame *head   = stream->queue;
	if (head != NULL) {
		assert(head->avpacket == NULL);
		frame         = head->avframe;
		stream->queue = head->next;
		if (stream->queue == NULL)
			stream->tail = NULL;
	}
	pthread_mutex_unlock(&stream->mutex);
	if (head != NULL)
		free(head);
	return frame;
}

static struct AVPacket *pop_packet(struct SStream *stream) {
	assert(stream != NULL);
	pthread_mutex_lock(&stream->mutex);
	if (stream->queue == NULL)
		pthread_cond_wait(&stream->cond, &stream->mutex);
	struct AVPacket *packet = NULL;
	struct SFrame *head     = stream->queue;
	if (head != NULL) {
		assert(head->avframe == NULL);
		packet        = head->avpacket;
		stream->queue = head->next;
		if (stream->queue == NULL)
			stream->tail = NULL;
	}
	pthread_mutex_unlock(&stream->mutex);
	if (head != NULL)
		free(head);
	return packet;
}

static void *stream_worker(void *arg) {
	struct SStream *stream = arg;
	if (stream->dir == SSTREAM_INPUT) {
		struct AVFrame *frame = av_frame_alloc();
		while (true) {
			struct AVPacket *packet = pop_packet(stream);
			if (packet == NULL) {  // close requested
				av_frame_free(&frame);
				return NULL;
			}
			int ret = avcodec_send_packet(stream->codec_ctx, packet);
			if (ret < 0) {
				perror("avcodec_send_packet");
				av_frame_free(&frame);
				return NULL;
			}
			while (ret == 0) {
				ret = avcodec_receive_frame(stream->codec_ctx, frame);
				if (ret == AVERROR(EAGAIN))
					break;
				else if (ret < 0) {
					perror("avcodec_receive_packet");
					break;
				}
				stream->media_handler(stream->part_id, frame);
				frame = av_frame_alloc();
			}
		}
	} else {
		struct AVPacket *packet = av_packet_alloc();
		while (true) {
			struct AVFrame *frame = pop_frame(stream);
			if (frame == NULL) {  // close requested
				av_packet_free(&packet);
				return NULL;
			}
			int ret = avcodec_send_frame(stream->codec_ctx, frame);
			if (ret < 0) {
				fprintf(stderr, "avcodec_send_frame: ret = %d\n", ret);
				av_packet_free(&packet);
				return NULL;
			}
			while (ret == 0) {
				ret = avcodec_receive_packet(stream->codec_ctx, packet);
				if (ret == AVERROR(EAGAIN))
					break;
				else if (ret < 0) {
					perror("avcodec_receive_packet");
					break;
				}
				stream->packet_handler(stream->part_id, packet);
				packet = av_packet_alloc();
			}
		}
	}
}

static void sstream_free(struct SStream **stream) {
	if (*stream == NULL)
		return;
	// clear queue
	pthread_mutex_lock(&(*stream)->mutex);
	while ((*stream)->queue != NULL) {
		struct SFrame *next = (*stream)->queue->next;
		av_frame_free(&(*stream)->queue->avframe);
		av_packet_free(&(*stream)->queue->avpacket);
		free((*stream)->queue);
		(*stream)->queue = next;
	}
	// wakeup worker thread
	pthread_cond_signal(&(*stream)->cond);
	pthread_mutex_unlock(&(*stream)->mutex);
	pthread_join((*stream)->handler_thread, NULL);
	avcodec_free_context(&(*stream)->codec_ctx);
	pthread_cond_destroy(&(*stream)->cond);
	pthread_mutex_destroy(&(*stream)->mutex);
	free(*stream);
	*stream = NULL;
}

static void stream_dump(FILE *fp, struct SStream *stream) {
	assert(stream != NULL);
	pthread_mutex_lock(&stream->mutex);
	int queue_len = 0;
	for (struct SFrame *f = stream->queue; f != NULL; f = f->next) queue_len++;
	if (stream->dir == SSTREAM_INPUT)
		fprintf(fp,
		        "{part=%llu %s queued %d packets}\n",
		        stream->part_id,
		        stream->type == SSTREAM_AUDIO ? "audio" : "video",
		        queue_len);
	else
		fprintf(fp,
		        "{%s queued %d frames}\n",
		        stream->type == SSTREAM_AUDIO ? "audio" : "video",
		        queue_len);
	pthread_mutex_unlock(&stream->mutex);
}

void scont_init(struct SStreamContainer *cont,
                media_handler_fn_t media_handler,
                packet_handler_fn_t packet_handler) {
	memset(cont, 0, sizeof(struct SStreamContainer));
	pthread_rwlock_init(&cont->mutex, NULL);
	cont->media_handler  = media_handler;
	cont->packet_handler = packet_handler;
}

void scont_free(struct SStreamContainer *cont) {
	// clean up all streams
	pthread_rwlock_wrlock(&cont->mutex);
	for (size_t i = 0; i < cont->nb_out; ++i) sstream_free(&cont->out[i]);
	cont->nb_out = 0;
	free(cont->out);
	cont->out = NULL;
	for (size_t i = 0; i < cont->nb_in; ++i) sstream_free(&cont->in[i]);
	cont->nb_in = 0;
	free(cont->in);
	cont->in = NULL;
	pthread_rwlock_unlock(&cont->mutex);
	pthread_rwlock_destroy(&cont->mutex);
}

void scont_dump(FILE *fp, struct SStreamContainer *cont) {
	pthread_rwlock_rdlock(&cont->mutex);
	fprintf(fp, "input streams:\n");
	for (int i = 0; i < cont->nb_in; ++i) {
		fprintf(fp, " - ");
		stream_dump(fp, cont->in[i]);
	}
	fprintf(fp, "output streams:\n");
	for (int i = 0; i < cont->nb_out; ++i) {
		fprintf(fp, " - ");
		stream_dump(fp, cont->in[i]);
	}
	pthread_rwlock_unlock(&cont->mutex);
}

static sstream_id_t sstream_create(enum SStreamType type, enum SStreamDirection dir) {
	struct SStream *stream = calloc(1, sizeof(struct SStream));
	if (stream == NULL)
		return stream;
	stream->type = type;
	stream->dir  = dir;

	// init codec context
	int codec_id =
	    type == SSTREAM_AUDIO ? SELECON_DEFAULT_AUDIO_CODEC_ID : SELECON_DEFAULT_VIDEO_CODEC_ID;
	const struct AVCodec *codec =
	    dir == SSTREAM_INPUT ? avcodec_find_decoder(codec_id) : avcodec_find_encoder(codec_id);
	if (codec == NULL) {
		perror("avcodec_find_coder");
		goto codec_err;
	}
	stream->codec_ctx = avcodec_alloc_context3(codec);
	if (stream->codec_ctx == NULL) {
		perror("avcodec_alloc_context3");
		goto codec_err;
	}

	// init handler thread stuff
	init_recursive_mutex(&stream->mutex);
	pthread_cond_init(&stream->cond, NULL);
	pthread_create(&stream->handler_thread, NULL, stream_worker, stream);
	return stream;
codec_err:
	free(stream);
	return NULL;
}

static void insert_stream(struct SStreamContainer *cont, struct SStream *stream) {
	pthread_rwlock_wrlock(&cont->mutex);
	if (stream->dir == SSTREAM_INPUT) {
		cont->in = reallocarray(cont->in, ++cont->nb_in, sizeof(struct SStream *));
		assert(cont->in != NULL);
		cont->in[cont->nb_in - 1] = stream;
	} else {
		cont->out = reallocarray(cont->out, ++cont->nb_out, sizeof(struct SStream *));
		assert(cont->out != NULL);
		cont->out[cont->nb_out - 1] = stream;
	}
	pthread_rwlock_unlock(&cont->mutex);
}

sstream_id_t scont_alloc_stream(struct SStreamContainer *cont,
                                part_id_t part_id,
                                enum SStreamType type,
                                enum SStreamDirection dir) {
	struct SStream *stream = sstream_create(type, dir);
	if (stream == NULL)
		return stream;
	stream->part_id = part_id;
	if (dir == SSTREAM_INPUT)
		stream->media_handler = cont->media_handler;
	else
		stream->packet_handler = cont->packet_handler;
	insert_stream(cont, stream);
	return stream;
}

void scont_close_stream(struct SStreamContainer *cont, sstream_id_t *stream) {
	if (*stream == NULL)
		return;
	pthread_rwlock_wrlock(&cont->mutex);
	for (size_t i = 0; i < cont->nb_in; ++i) {
		if (cont->in[i] == *stream) {
			if (i + 1 < cont->nb_in)
				memmove(&cont->in[i], &cont->in[i + 1], cont->nb_in - i - 1);
			cont->nb_in--;
			pthread_rwlock_unlock(&cont->mutex);
			*stream = NULL;
			return;
		}
	}
	for (size_t i = 0; i < cont->nb_out; ++i) {
		if (cont->out[i] == *stream) {
			if (i + 1 < cont->nb_out)
				memmove(&cont->out[i], &cont->out[i + 1], cont->nb_out - i - 1);
			cont->nb_out--;
			pthread_rwlock_unlock(&cont->mutex);
			*stream = NULL;
			return;
		}
	}
	pthread_rwlock_unlock(&cont->mutex);
	fprintf(stderr, "scont_close_stream - invalid stream id\n");
}

bool scont_has_stream(struct SStreamContainer *cont, sstream_id_t stream) {
	pthread_rwlock_rdlock(&cont->mutex);
	bool has = false;
	for (size_t i = 0; i < cont->nb_in && !has; ++i) has = cont->in[i] == stream;
	for (size_t i = 0; i < cont->nb_out && !has; ++i) has = cont->out[i] == stream;
	pthread_rwlock_unlock(&cont->mutex);
	return has;
}

bool scont_stream_closed(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_empty(struct SStreamContainer *cont, sstream_id_t stream) {
	return !scont_has_stream(cont, stream) || stream->queue == NULL;
}

enum SError scont_push_frame(struct SStreamContainer *cont,
                             sstream_id_t stream,
                             struct AVFrame *frame) {
	enum SError err = SELECON_OK;
	pthread_rwlock_rdlock(&cont->mutex);
	if (!scont_has_stream(cont, stream))
		err = SELECON_INVALID_STREAM;
	else {
		// check stream type and frame type
		assert((stream->type == SSTREAM_AUDIO && frame->nb_samples > 0) ||
		       (stream->type == SSTREAM_VIDEO && frame->nb_samples == 0));
		insert_frame(stream, frame);
	}
	pthread_rwlock_unlock(&cont->mutex);
	return err;
}

enum SError scont_push_packet(struct SStreamContainer *cont,
                              sstream_id_t stream,
                              struct AVPacket *packet) {
	enum SError err = SELECON_OK;
	pthread_rwlock_rdlock(&cont->mutex);
	if (!scont_has_stream(cont, stream))
		err = SELECON_INVALID_STREAM;
	else
		insert_packet(stream, packet);
	pthread_rwlock_unlock(&cont->mutex);
	return err;
}
