#include "stream.h"

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/codec_id.h>
#include <libavutil/frame.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int init_recursive_mutex(pthread_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	int ret = pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	return ret;
}

static void insert_frame(struct SStream *stream, struct AVFrame *frame) {
	struct SFrame *sframe = calloc(1, sizeof(struct SFrame));
	sframe->avframe       = frame;
	if (stream->tail == NULL)
		stream->queue = sframe;
	else
		stream->tail->next = sframe;
	stream->tail = sframe;
}

static void sstream_free(struct SStream **stream) {
	avcodec_free_context(&(*stream)->codecCtx);
	while ((*stream)->queue != NULL) {
		struct SFrame *next = (*stream)->queue->next;
		av_frame_free(&(*stream)->queue->avframe);
		free((*stream)->queue);
		(*stream)->queue = next;
	}
	free(*stream);
	*stream = NULL;
}

void scont_init(struct SStreamContainer *cont) {
	memset(cont, 0, sizeof(struct SStreamContainer));
	init_recursive_mutex(&cont->mutex);
}

void scont_free(struct SStreamContainer *cont) {
	// clean up all streams
	pthread_mutex_lock(&cont->mutex);
	for (size_t i = 0; i < cont->nb_out; ++i) sstream_free(&cont->out[i]);
	cont->nb_out = 0;
	free(cont->out);
	cont->out = NULL;
	for (size_t i = 0; i < cont->nb_in; ++i) sstream_free(&cont->in[i]);
	cont->nb_in = 0;
	free(cont->in);
	cont->in = NULL;
	pthread_mutex_unlock(&cont->mutex);
	pthread_mutex_destroy(&cont->mutex);
}

sstream_id_t scont_alloc_audio_stream(struct SStreamContainer *cont, enum SStreamDirection dir) {
	struct SStream *stream = calloc(1, sizeof(struct SStream));
	assert(stream != NULL);
	stream->type = SSTREAM_AUDIO;
	stream->dir  = dir;

	// init codec context
	const struct AVCodec *codec = NULL;
	if (dir == SSTREAM_INPUT) {
		codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
		if (codec == NULL) {
			perror("avcodec_find_decoder");
			goto codec_err;
		}
	} else {
		codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
		if (codec == NULL) {
			perror("avcodec_find_encoder");
			goto codec_err;
		}
	}
	stream->codecCtx = avcodec_alloc_context3(codec);
	if (stream->codecCtx == NULL) {
		perror("avcodec_alloc_context3");
		goto codec_err;
	}

	pthread_mutex_lock(&cont->mutex);
	if (dir == SSTREAM_INPUT) {
		cont->in = reallocarray(cont->in, ++cont->nb_in, sizeof(struct SStream *));
		assert(cont->in != NULL);
		cont->in[cont->nb_in - 1] = stream;
	} else {
		cont->out = reallocarray(cont->out, ++cont->nb_out, sizeof(struct SStream *));
		assert(cont->out != NULL);
		cont->out[cont->nb_out - 1] = stream;
	}
	pthread_mutex_unlock(&cont->mutex);
	return stream;
codec_err:
	free(stream);
	return NULL;
}

sstream_id_t scont_alloc_video_stream(struct SStreamContainer *cont,
                                      enum SStreamDirection dir,
                                      size_t width,
                                      size_t height) {
	struct SStream *stream = calloc(1, sizeof(struct SStream));
	assert(stream != NULL);
	stream->type   = SSTREAM_VIDEO;
	stream->width  = width;
	stream->height = height;
	stream->dir    = dir;
	if (dir == SSTREAM_INPUT) {
		cont->in = reallocarray(cont->in, ++cont->nb_in, sizeof(struct SStream *));
		assert(cont->in != NULL);
		cont->in[cont->nb_in - 1] = stream;
	} else {
		cont->out = reallocarray(cont->out, ++cont->nb_out, sizeof(struct SStream *));
		assert(cont->out != NULL);
		cont->out[cont->nb_out - 1] = stream;
	}
	return stream;
}

void scont_close_stream(struct SStreamContainer *cont, sstream_id_t stream) {
	pthread_mutex_lock(&cont->mutex);
	for (size_t i = 0; i < cont->nb_in; ++i) {
		if (cont->in[i] == stream) {
			if (i + 1 < cont->nb_in)
				memmove(&cont->in[i], &cont->in[i + 1], cont->nb_in - i - 1);
			cont->nb_in--;
			pthread_mutex_unlock(&cont->mutex);
			return;
		}
	}
	for (size_t i = 0; i < cont->nb_out; ++i) {
		if (cont->out[i] == stream) {
			if (i + 1 < cont->nb_out)
				memmove(&cont->out[i], &cont->out[i + 1], cont->nb_out - i - 1);
			cont->nb_out--;
			pthread_mutex_unlock(&cont->mutex);
			return;
		}
	}
	pthread_mutex_unlock(&cont->mutex);
	fprintf(stderr, "scont_close_stream - invalid stream id\n");
}

bool scont_has_stream(struct SStreamContainer *cont, sstream_id_t stream) {
	pthread_mutex_lock(&cont->mutex);
	bool has = false;
	for (size_t i = 0; i < cont->nb_in && !has; ++i) has = cont->in[i] == stream;
	for (size_t i = 0; i < cont->nb_out && !has; ++i) has = cont->out[i] == stream;
	pthread_mutex_unlock(&cont->mutex);
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
	pthread_mutex_lock(&cont->mutex);
	if (!scont_has_stream(cont, stream))
		err = SELECON_INVALID_STREAM;
	else {
		// check stream type and frame type
		if (stream->type == SSTREAM_AUDIO && frame->nb_samples > 0) {
			insert_frame(stream, frame);
		} else if (stream->type == SSTREAM_VIDEO && frame->format == AV_PIX_FMT_RGBA) {
			insert_frame(stream, frame);
		} else {
			err = SELECON_INVALID_STREAM;
		}
	}
	pthread_mutex_unlock(&cont->mutex);
	return err;
}
