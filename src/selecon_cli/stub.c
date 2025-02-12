#define _GNU_SOURCE

#include "stub.h"

#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "selecon.h"
#include "stime.h"

struct Stub {
	bool close_requested;
	struct SContext* context;
	pthread_t worker_thread;
	pthread_mutex_t mutex;
	char* filename;
};

static void stub_read_file(struct Stub* stub,
                           struct AVFormatContext* fmt_ctx,
                           struct AVCodecContext* a_ctx,
                           struct AVCodecContext* v_ctx,
                           struct AVStream* a_stream,
                           struct AVStream* v_stream) {
	// allocate selecon streams
	sstream_id_t audio_stream_id = NULL;
	sstream_id_t video_stream_id = NULL;
	if (a_stream != NULL) {
		enum SError err = selecon_stream_alloc_audio(stub->context, &audio_stream_id);
		if (err != SELECON_OK) {
			perror("selecon_stream_alloc_audio");
			return;
		}
	}
	if (v_stream != NULL) {
		enum SError err = selecon_stream_alloc_video(stub->context, &video_stream_id);
		if (err != SELECON_OK) {
			perror("selecon_stream_alloc_video");
			selecon_stream_free(stub->context, &audio_stream_id);
			return;
		}
	}
	int audio_stream_index  = a_stream == NULL ? -1 : a_stream->index;
	int video_stream_index  = v_stream == NULL ? -1 : v_stream->index;
	struct AVPacket* packet = av_packet_alloc();
	struct AVFrame* frame   = av_frame_alloc();
	while (!stub->close_requested) {
		if (av_read_frame(fmt_ctx, packet) < 0) {
			if (errno == 0) {
				avformat_seek_file(fmt_ctx,
				                   a_stream != NULL ? audio_stream_index : video_stream_index,
				                   0,
				                   0,
				                   0,
				                   0);
				continue;
			}
			perror("av_read_frame");
			goto free_frames;
		} else if (packet->stream_index != audio_stream_index &&
		           packet->stream_index != video_stream_index) {
			av_packet_unref(packet);
			continue;
		}
		struct AVStream* stream = packet->stream_index == audio_stream_index ? a_stream : v_stream;
		struct AVCodecContext* codecCtx =
		    packet->stream_index == audio_stream_index ? a_ctx : v_ctx;
		sstream_id_t stream_id =
		    packet->stream_index == audio_stream_index ? audio_stream_id : video_stream_id;
		if (avcodec_send_packet(codecCtx, packet) < 0) {
			perror("avcodec_send_packet");
			goto free_frames;
		}
		int64_t ts_delta = 0;
		while (!stub->close_requested) {
			int ret = avcodec_receive_frame(codecCtx, frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0) {
				perror("avcodec_receive_frame");
				goto free_frames;
			}
			// update timestamp only for audio, if it is presented, otherwise - only for video
			if (a_stream == NULL || packet->stream_index == audio_stream_index) {
				// printf("+ dur = %ld\n", frame->pkt_duration);
				ts_delta += frame->pkt_duration;
			}
			frame->time_base = stream->time_base;
			frame->pts =
			    av_rescale_q(get_curr_timestamp() / 1000,
			                 av_make_q(1, 1000000),
			                 packet->stream_index == audio_stream_index ? a_stream->time_base
			                                                            : v_stream->time_base);
			if (packet->stream_index == video_stream_index)
				frame->pict_type = AV_PICTURE_TYPE_NONE;  // reset picture type
				                                          // frame timestamp correct here
			enum SError err = selecon_stream_push_frame(stub->context, stream_id, &frame);
			if (err != SELECON_OK) {
				perror("selecon_stream_push_frame");
				goto free_frames;
			}
			frame = av_frame_alloc();
		}
		av_packet_unref(packet);
		// realtime delay emulation
		int64_t time_delta =
		    av_rescale_q(ts_delta,
		                 a_stream != NULL ? a_stream->time_base : v_stream->time_base,
		                 av_make_q(1, 1000000));
		// printf("time delta: %ld\n", time_delta);
		usleep(time_delta);
	}
free_frames:
	av_frame_free(&frame);
	av_packet_free(&packet);
	selecon_stream_free(stub->context, &video_stream_id);
	selecon_stream_free(stub->context, &audio_stream_id);
}

static void* stub_dynamic_worker(void* arg) {
	struct Stub* stub               = arg;
	struct AVFormatContext* fmt_ctx = NULL;
	if (avformat_open_input(&fmt_ctx, stub->filename, NULL, NULL) != 0) {
		perror("avformat_open_input");
		return NULL;
	}
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		perror("avformat_find_stream_info");
		goto close_input;
	}
	const struct AVCodec* acodec = NULL;
	const struct AVCodec* vcodec = NULL;
	int astream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &acodec, 0);
	int vstream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
	if (astream_index < 0 && vstream_index < 0) {
		perror("av_find_best_stream");
		goto close_input;
	}
	struct AVCodecContext* a_ctx = NULL;
	struct AVStream* a_stream    = NULL;
	if (acodec != NULL) {
		a_ctx = avcodec_alloc_context3(acodec);
		if (a_ctx == NULL) {
			perror("avcodec_alloc_context3");
			goto close_codec;
		}
		a_stream = fmt_ctx->streams[astream_index];
		avcodec_parameters_to_context(a_ctx, a_stream->codecpar);
		if (avcodec_open2(a_ctx, acodec, NULL) < 0) {
			perror("avcodec_open2");
			goto close_codec;
		}
	}
	struct AVCodecContext* v_ctx = NULL;
	struct AVStream* v_stream    = NULL;
	if (vcodec != NULL) {
		v_ctx = avcodec_alloc_context3(vcodec);
		if (v_ctx == NULL) {
			perror("avcodec_alloc_context3");
			goto close_codec;
		}
		v_stream = fmt_ctx->streams[vstream_index];
		avcodec_parameters_to_context(v_ctx, v_stream->codecpar);
		if (avcodec_open2(v_ctx, vcodec, NULL) < 0) {
			perror("avcodec_open2");
			goto close_codec;
		}
	}
	printf("start decoding file\n");
	stub_read_file(stub, fmt_ctx, a_ctx, v_ctx, a_stream, v_stream);
	printf("end decoding file\n");
close_codec:
	avcodec_free_context(&v_ctx);
	avcodec_free_context(&a_ctx);
close_input:
	avformat_close_input(&fmt_ctx);
	return NULL;
}

struct Stub* stub_create(struct SContext* context, const char* filename, void* (*worker)(void*)) {
	struct Stub* stub = calloc(1, sizeof(struct Stub));
	stub->context     = context;
	stub->filename    = strdup(filename);
	pthread_mutex_init(&stub->mutex, NULL);
	if (pthread_create(&stub->worker_thread, NULL, worker, stub) != 0) {
		pthread_mutex_destroy(&stub->mutex);
		free(stub->filename);
		free(stub);
		return NULL;
	}
	pthread_setname_np(stub->worker_thread, "stub");
	return stub;
}

struct Stub* stub_create_dynamic(struct SContext* context, const char* media_file) {
	return stub_create(context, media_file, stub_dynamic_worker);
}

void stub_close(struct Stub** stub) {
	if (*stub == NULL || (*stub)->close_requested)
		return;
	pthread_mutex_lock(&(*stub)->mutex);
	(*stub)->close_requested = true;
	pthread_mutex_unlock(&(*stub)->mutex);
	pthread_join((*stub)->worker_thread, NULL);
	pthread_mutex_destroy(&(*stub)->mutex);
	free((*stub)->filename);
	free(*stub);
	*stub = NULL;
}
