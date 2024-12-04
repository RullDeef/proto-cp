#pragma once

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "stypes.h"

enum SStreamType {
	SSTREAM_AUDIO,
	SSTREAM_VIDEO,
};

enum SStreamDirection {
	SSTREAM_INPUT,
	SSTREAM_OUTPUT,
};

struct SFrame {
	struct AVFrame *avframe;
	struct SFrame *next;
};

struct SStream {
	enum SStreamType type;
	enum SStreamDirection dir;
	// ? part_id_t part_id;

	// for video streams
	size_t width;
	size_t height;

	struct AVCodecContext *codecCtx;

	// if this is input stream - queue holds recvd frames from paired participant.
	// if this is output stream - queue holds frames ready to be send
	struct SFrame *queue;
	struct SFrame *tail;
};

struct SStreamContainer {
	// outgoing data streams. Client submits audio/video data through this streams
	// and then worker threads send media to other participants.
	struct SStream **in;
	size_t nb_in;

	// ingoing data streams. Upon successfull handshake between clients each of them
	// creates out stream to store received media frames. Client then can be
	// notified or can manually drain data from them
	struct SStream **out;
	size_t nb_out;

	// mutex for exclusive access to streams arrays
	pthread_mutex_t mutex;
};

void scont_init(struct SStreamContainer *cont);
void scont_free(struct SStreamContainer *cont);

sstream_id_t scont_alloc_audio_stream(struct SStreamContainer *cont, enum SStreamDirection dir);

sstream_id_t scont_alloc_video_stream(struct SStreamContainer *cont,
                                      enum SStreamDirection dir,
                                      size_t width,
                                      size_t height);

void scont_close_stream(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_has_stream(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_closed(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_empty(struct SStreamContainer *cont, sstream_id_t stream);

enum SError scont_push_frame(struct SStreamContainer *cont,
                             sstream_id_t stream,
                             struct AVFrame *frame);
