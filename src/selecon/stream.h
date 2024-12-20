#pragma once

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavfilter/avfilter.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "error.h"
#include "media_filters.h"
#include "participant.h"
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
	struct AVFrame *avframe;    // valid for output streams
	struct AVPacket *avpacket;  // valid for input streams
	struct SFrame *next;
};

struct SStream {
	enum SStreamType type;
	enum SStreamDirection dir;
	part_id_t part_id;

	// callback valid for input streams. Called for each received media frame
	media_handler_fn_t media_handler;
	void *media_user_data;

	// callback valid for output streams. Called for each encoded media packet
	packet_handler_fn_t packet_handler;
	void *packet_user_data;

	struct AVCodecContext *codec_ctx;
	struct SwrContext *swr_context;  // resampling to default audio format
	struct MediaFilterGraph filter_graph;

	// if this is input stream - queue holds recvd frames from paired participant.
	// if this is output stream - queue holds frames ready to be send
	struct SFrame *queue;
	struct SFrame *tail;
	pthread_mutex_t mutex;
	pthread_cond_t cond;  // handler_thread must wait until queue is not empty

	// worker thread that does encoding/decoding job and calls callbacks
	pthread_t handler_thread;
};

struct SStreamContainer {
	media_handler_fn_t media_handler;
	packet_handler_fn_t packet_handler;
	void *user_data;

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
	pthread_rwlock_t mutex;
};

void scont_init(struct SStreamContainer *cont,
                media_handler_fn_t media_handler,
                packet_handler_fn_t packet_handler,
                struct SContext *user_data);
void scont_free(struct SStreamContainer *cont);

void scont_dump(FILE *fp, struct SStreamContainer *cont);

sstream_id_t scont_alloc_stream(struct SStreamContainer *cont,
                                part_id_t part_id,
                                enum SStreamType type,
                                enum SStreamDirection dir);

void scont_close_stream(struct SStreamContainer *cont, sstream_id_t *stream);

// usable when participant disconnects
void scont_close_streams(struct SStreamContainer *cont, part_id_t part_id);

bool scont_has_stream(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_closed(struct SStreamContainer *cont, sstream_id_t stream);

bool scont_stream_empty(struct SStreamContainer *cont, sstream_id_t stream);

sstream_id_t scont_find_stream(struct SStreamContainer *cont,
                               part_id_t part_id,
                               enum SStreamType type,
                               enum SStreamDirection dir);

enum SError scont_push_frame(struct SStreamContainer *cont,
                             sstream_id_t stream,
                             struct AVFrame *frame);

enum SError scont_push_packet(struct SStreamContainer *cont,
                              sstream_id_t stream,
                              struct AVPacket *packet);
