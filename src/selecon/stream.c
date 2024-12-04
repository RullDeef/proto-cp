#include "stream.h"

#include <assert.h>
#include <string.h>

static int init_recursive_mutex(pthread_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	int ret = pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
  return ret;
}

static void sstream_free(struct SStream *stream) {

}

void scont_init(struct SStreamContainer *cont) {
  memset(cont, 0, sizeof(struct SStreamContainer));
  init_recursive_mutex(&cont->mutex);
}

void scont_free(struct SStreamContainer *cont) {
  // clean up all streams
  pthread_mutex_lock(&cont->mutex);
  for (size_t i = 0; i < cont->nb_out; ++i)
    sstream_free(cont->out[i]);
  cont->nb_out = 0;
  free(cont->out);
  cont->out = NULL;
  for (size_t i = 0; i < cont->nb_in; ++i)
    sstream_free(cont->in[i]);
  cont->nb_in = 0;
  free(cont->in);
  cont->in = NULL;
  pthread_mutex_unlock(&cont->mutex);
  pthread_mutex_destroy(&cont->mutex);
}

sstream_id_t scont_alloc_audio_stream(struct SStreamContainer *cont,
                                      enum SStreamDirection dir) {
  struct SStream *stream = calloc(1, sizeof(struct SStream));
  assert(stream != NULL);
  stream->type = SSTREAM_AUDIO;
  stream->dir = dir;
  pthread_mutex_lock(&cont->mutex);
  if (dir == SSTREAM_INPUT) {
    cont->in = reallocarray(cont->in, ++cont->nb_in, sizeof(struct SStream*));
    assert(cont->in != NULL);
    cont->in[cont->nb_in - 1] = stream;
  } else {
    cont->out = reallocarray(cont->out, ++cont->nb_out, sizeof(struct SStream*));
    assert(cont->out != NULL);
    cont->out[cont->nb_out - 1] = stream;
  }
  pthread_mutex_unlock(&cont->mutex);
  return stream;
}

sstream_id_t scont_alloc_video_stream(struct SStreamContainer *cont,
                                      enum SStreamDirection dir,
                                      size_t width, size_t height) {
  struct SStream *stream = calloc(1, sizeof(struct SStream));
  assert(stream != NULL);
  stream->type = SSTREAM_VIDEO;
  stream->width = width;
  stream->height = height;
  stream->dir = dir;
  if (dir == SSTREAM_INPUT) {
    cont->in = reallocarray(cont->in, ++cont->nb_in, sizeof(struct SStream*));
    assert(cont->in != NULL);
    cont->in[cont->nb_in - 1] = stream;
  } else {
    cont->out = reallocarray(cont->out, ++cont->nb_out, sizeof(struct SStream*));
    assert(cont->out != NULL);
    cont->out[cont->nb_out - 1] = stream;
  }
  return stream;
}

void scont_close_stream(struct SStreamContainer *cont, sstream_id_t stream) {
  for (size_t i = 0; i < cont->nb_in; ++i) {
    if (cont->in[i] == stream) {
      if (i + 1 < cont->nb_in)
        memmove(&cont->in[i], &cont->in[i + 1], cont->nb_in - i - 1);
      cont->nb_in--;
      return;
    }
  }
  for (size_t i = 0; i < cont->nb_out; ++i) {
    if (cont->out[i] == stream) {
      if (i + 1 < cont->nb_out)
        memmove(&cont->out[i], &cont->out[i + 1], cont->nb_out - i - 1);
      cont->nb_out--;
      return;
    }
  }
}

bool scont_has_stream(struct SStreamContainer *cont, sstream_id_t stream) {
  pthread_mutex_lock(&cont->mutex);
  bool has = false;
  for (size_t i = 0; i < cont->nb_in && !has; ++i)
    has = cont->in[i] == stream;
  for (size_t i = 0; i < cont->nb_out && !has; ++i)
    has = cont->out[i] == stream;
  pthread_mutex_unlock(&cont->mutex);
  return has;
}

bool scont_stream_closed(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_empty(struct SStreamContainer *cont, sstream_id_t stream);

enum SError scont_stream_push(struct SStreamContainer *cont, sstream_id_t stream, struct AVFrame *frame) {
  enum SError err = SELECON_OK;
  pthread_mutex_lock(&cont->mutex);
  if (!scont_has_stream(cont, stream))
    err = SELECON_INVALID_STREAM;
  else {
    // check stream type and frame type
    if (stream->type == SSTREAM_AUDIO && frame->nb_samples > 0) {
      stream->
    } else if (stream->type == SSTREAM_VIDEO && frame->format == AV_PIX_FMT_RGBA) {

    } else {
      err = SELECON_INVALID_STREAM;
    }
  }
  pthread_mutex_unlock(&cont->mutex);
  return err;
}