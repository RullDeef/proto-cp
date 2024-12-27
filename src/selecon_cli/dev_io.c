#define _GNU_SOURCE

#include "dev_io.h"

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <pthread.h>
#include <time.h>

#include "avutility.h"
#include "config.h"
#include "debugging.h"
#include "media_filters.h"
#include "selecon.h"

enum DevPairType { INPUT, OUTPUT };

struct DevPairInput {
	bool close_requested;
	struct SContext* context;
	char* adev_name;
	char* vdev_name;
	pthread_t audio_worker_thread;
	pthread_t video_worker_thread;
};

struct DevPairOutput {
	struct AVFormatContext* afmt_ctx;
	struct MediaFilterGraph agraph;
	struct AVFormatContext* vfmt_ctx;
	// struct MediaFilterGraph vgraph;
};

struct Dev {
	enum DevPairType type;
	union {
		struct DevPairInput input;
		struct DevPairOutput output;
	};
};

static const AVInputFormat* find_input_audio_fmt(const char* dev_name) {
	if (dev_name != NULL) {
		const AVInputFormat* in_fmt = NULL;
		while ((in_fmt = av_input_audio_device_next(in_fmt)) != NULL)
			if (strcmp(in_fmt->name, dev_name) == 0)
				return in_fmt;
		// repeat with inexact matching
		while ((in_fmt = av_input_audio_device_next(in_fmt)) != NULL)
			if (strstr(in_fmt->name, dev_name) != NULL)
				return in_fmt;
		fprintf(stderr, "input audio device '%s' not found\n", dev_name);
	}
	return NULL;
}

static const AVInputFormat* find_input_video_fmt(const char* dev_name) {
	if (dev_name != NULL) {
		const AVInputFormat* in_fmt = NULL;
		while ((in_fmt = av_input_video_device_next(in_fmt)) != NULL)
			if (strcmp(in_fmt->name, dev_name) == 0)
				return in_fmt;
		// repeat with inexact matching
		while ((in_fmt = av_input_video_device_next(in_fmt)) != NULL)
			if (strstr(in_fmt->name, dev_name) != NULL)
				return in_fmt;
		fprintf(stderr, "input video device '%s' not found\n", dev_name);
	}
	return NULL;
}

static const AVOutputFormat* find_output_audio_fmt(const char* dev_name) {
	if (dev_name != NULL) {
		const AVOutputFormat* in_fmt = NULL;
		while ((in_fmt = av_output_audio_device_next(in_fmt)) != NULL)
			if (strcmp(in_fmt->name, dev_name) == 0)
				return in_fmt;
		// repeat with inexact matching
		while ((in_fmt = av_output_audio_device_next(in_fmt)) != NULL)
			if (strstr(in_fmt->name, dev_name) != NULL)
				return in_fmt;
		fprintf(stderr, "output audio device '%s' not found\n", dev_name);
	}
	return NULL;
}

static const AVOutputFormat* find_output_video_fmt(const char* dev_name) {
	if (dev_name != NULL) {
		const AVOutputFormat* in_fmt = NULL;
		while ((in_fmt = av_output_video_device_next(in_fmt)) != NULL)
			if (strcmp(in_fmt->name, dev_name) == 0)
				return in_fmt;
		// repeat with inexact matching
		while ((in_fmt = av_output_video_device_next(in_fmt)) != NULL)
			if (strstr(in_fmt->name, dev_name) != NULL)
				return in_fmt;
		fprintf(stderr, "output video device '%s' not found\n", dev_name);
	}
	return NULL;
}

static inline long nanosec_delta(struct timespec start, struct timespec end) {
	int sec_delta = end.tv_sec - start.tv_sec;
	return sec_delta * 1000000000L + (end.tv_nsec - start.tv_nsec);
}

static void input_audio_worker2(struct DevPairInput* dev,
                                struct AVFormatContext* afmt_ctx,
                                struct AVCodecContext* acodec_ctx) {
	sstream_id_t astream = NULL;
	enum SError err      = selecon_stream_alloc_audio(dev->context, &astream);
	if (err != SELECON_OK) {
		fprintf(stderr, "failed to allocate audio stream: %s\n", serror_str(err));
		return;
	}
	struct AVFrame* frame   = av_frame_alloc();
	struct AVPacket* packet = av_packet_alloc();
	struct timespec ts_start, ts_end;
	while (!dev->close_requested) {
		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		int ret = av_read_frame(afmt_ctx, packet);
		if (ret < 0) {
			fprintf(stderr, "failed to read audio data from device: ret = %d\n", ret);
			break;
		}
		avcodec_send_packet(acodec_ctx, packet);
		ret                 = avcodec_receive_frame(acodec_ctx, frame);
		int64_t samples_got = 0;
		while (ret == 0) {
			samples_got += frame->nb_samples;
			selecon_stream_push_frame(dev->context, astream, &frame);
			frame = av_frame_alloc();
			ret   = avcodec_receive_frame(acodec_ctx, frame);
		}
		assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
		av_packet_unref(packet);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		int64_t delta =
		    1000000000 / acodec_ctx->sample_rate * samples_got - nanosec_delta(ts_start, ts_end);
		if (delta > 0)
			usleep(delta / 1000);
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
	selecon_stream_free(dev->context, &astream);
}

static void input_video_worker2(struct DevPairInput* dev,
                                struct AVFormatContext* vfmt_ctx,
                                struct AVCodecContext* vcodec_ctx) {
	sstream_id_t vstream = NULL;
	enum SError err      = selecon_stream_alloc_video(dev->context, &vstream);
	if (err != SELECON_OK) {
		fprintf(stderr, "failed to allocate video stream: %s\n", serror_str(err));
		return;
	}
	struct AVFrame* frame   = av_frame_alloc();
	struct AVPacket* packet = av_packet_alloc();
	struct timespec ts_start, ts_end;
	while (!dev->close_requested) {
		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		int ret = av_read_frame(vfmt_ctx, packet);
		if (ret < 0) {
			fprintf(stderr, "failed to read video data from device: ret = %d\n", ret);
			break;
		}
		avcodec_send_packet(vcodec_ctx, packet);
		ret = avcodec_receive_frame(vcodec_ctx, frame);
		while (ret == 0) {
			frame->sample_aspect_ratio = vfmt_ctx->streams[0]->sample_aspect_ratio;
			selecon_stream_push_frame(dev->context, vstream, &frame);
			frame = av_frame_alloc();
			ret   = avcodec_receive_frame(vcodec_ctx, frame);
		}
		assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
		av_packet_unref(packet);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		int delta = 1000000000 / 30 - nanosec_delta(ts_start, ts_end);
		if (delta > 0)
			usleep(delta / 1000);
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
	selecon_stream_free(dev->context, &vstream);
}

static void* input_audio_worker(void* arg) {
	struct DevPairInput* dev = arg;
	if (dev->adev_name == NULL)
		return NULL;
	const AVInputFormat* afmt        = find_input_audio_fmt(dev->adev_name);
	struct AVFormatContext* afmt_ctx = NULL;
	if (avformat_open_input(&afmt_ctx, NULL, afmt, NULL) < 0) {
		fprintf(stderr, "failed to open audio input\n");
		return NULL;
	}
	const struct AVCodec* acodec      = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);
	struct AVCodecContext* acodec_ctx = avcodec_alloc_context3(acodec);
	acodec_ctx->sample_fmt            = AV_SAMPLE_FMT_S16;
	acodec_ctx->sample_rate           = 48000;
	av_channel_layout_default(&acodec_ctx->ch_layout, 2);
	int ret = avcodec_open2(acodec_ctx, acodec, NULL);
	if (ret < 0)
		fprintf(stderr, "failed to open audio device decoder: ret = %d\n", ret);
	else {
		input_audio_worker2(dev, afmt_ctx, acodec_ctx);
		avcodec_free_context(&acodec_ctx);
	}
	avformat_close_input(&afmt_ctx);
	return NULL;
}

static void* input_video_worker(void* arg) {
	struct DevPairInput* dev = arg;
	if (dev->vdev_name == NULL)
		return NULL;
	const AVInputFormat* vfmt        = find_input_video_fmt(dev->vdev_name);
	struct AVFormatContext* vfmt_ctx = NULL;
	if (avformat_open_input(&vfmt_ctx, NULL, vfmt, NULL) < 0) {
		fprintf(stderr, "failed to open video input\n");
		return NULL;
	}
	const struct AVCodec* vcodec      = avcodec_find_decoder(AV_CODEC_ID_RAWVIDEO);
	struct AVCodecContext* vcodec_ctx = avcodec_alloc_context3(vcodec);
	vcodec_ctx->pix_fmt               = AV_PIX_FMT_YUV420P;
	vcodec_ctx->width                 = SELECON_DEFAULT_VIDEO_WIDTH;
	vcodec_ctx->height                = SELECON_DEFAULT_VIDEO_HEIGHT;
	int ret                           = avcodec_open2(vcodec_ctx, vcodec, NULL);
	if (ret < 0)
		fprintf(stderr, "failed to open video devoce decoder: ret = %d\n", ret);
	else {
		input_video_worker2(dev, vfmt_ctx, vcodec_ctx);
		avcodec_free_context(&vcodec_ctx);
	}
	avformat_close_input(&vfmt_ctx);
	return NULL;
}

// allocates output streams for each non-NULL dev_name and starts sending frames to streams
struct Dev* dev_create_src(struct SContext* context, const char* adev_name, const char* vdev_name) {
	struct Dev* dev      = calloc(1, sizeof(struct Dev));
	dev->type            = INPUT;
	dev->input.context   = context;
	dev->input.adev_name = adev_name == NULL ? NULL : strdup(adev_name);
	dev->input.vdev_name = vdev_name == NULL ? NULL : strdup(vdev_name);
	pthread_create(&dev->input.audio_worker_thread, NULL, input_audio_worker, &dev->input);
	// TODO: err code check
	pthread_setname_np(dev->input.audio_worker_thread, "adev_in");
	pthread_create(&dev->input.video_worker_thread, NULL, input_video_worker, &dev->input);
	// TODO: err code check
	pthread_setname_np(dev->input.video_worker_thread, "vdev_in");
	return dev;
}

// prepares output devices for each non-NULL dev_name
struct Dev* dev_create_sink(const char* adev_name, const char* vdev_name) {
	struct Dev* dev = calloc(1, sizeof(struct Dev));
	dev->type       = OUTPUT;
	// prepare audio output context
	const AVOutputFormat* afmt = find_output_audio_fmt(adev_name);
	int ret = avformat_alloc_output_context2(&dev->output.afmt_ctx, afmt, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to open output audio device: err = %d\n", ret);
		free(dev);
		return NULL;
	}
	// create audio stream
	struct AVStream* astream       = avformat_new_stream(dev->output.afmt_ctx, NULL);
	astream->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
	astream->codecpar->sample_rate = 48000;
	astream->codecpar->format      = AV_SAMPLE_FMT_S16;
	av_channel_layout_default(&astream->codecpar->ch_layout, 2);
	// commit
	ret = avformat_write_header(dev->output.afmt_ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to write audio header: ret = %d\n", ret);
		goto cleanup;
	}
	// create audio filter graph
	mfgraph_init_audio(&dev->output.agraph, AV_SAMPLE_FMT_S16, 48000, 2, 1024);
	// prepare video, if selected
	if (vdev_name != NULL) {
		const AVOutputFormat* vfmt = find_output_video_fmt(vdev_name);
		int ret = avformat_alloc_output_context2(&dev->output.vfmt_ctx, vfmt, NULL, NULL);
		if (ret < 0) {
			fprintf(stderr, "fialed to open output video device: err = %d\n", ret);
			goto cleanup_audio;
		}
		// create video stream
		struct AVStream* vstream      = avformat_new_stream(dev->output.vfmt_ctx, NULL);
		vstream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
		vstream->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
		vstream->codecpar->width      = SELECON_DEFAULT_VIDEO_WIDTH;
		vstream->codecpar->height     = SELECON_DEFAULT_VIDEO_HEIGHT;
		vstream->codecpar->format     = SELECON_DEFAULT_VIDEO_PIXEL_FMT;
		// commit
		ret = avformat_write_header(dev->output.vfmt_ctx, NULL);
		if (ret < 0) {
			fprintf(stderr, "failed to write video header: ret = %d\n", ret);
			goto cleanup_video;
		}
	}
	return dev;
cleanup_video:
	avformat_free_context(dev->output.vfmt_ctx);
cleanup_audio:
	mfgraph_free(&dev->output.agraph);
cleanup:
	avformat_free_context(dev->output.afmt_ctx);
	free(dev);
	return NULL;
}

// push received frames to output devices
void dev_push_frame(struct Dev* dev, enum AVMediaType mtype, struct AVFrame* frame) {
	assert(dev->type == OUTPUT);
	if (mtype == AVMEDIA_TYPE_AUDIO) {
		mfgraph_send(&dev->output.agraph, frame);
		int ret = mfgraph_receive(&dev->output.agraph, frame);
		while (ret == 0) {
			ret = av_interleaved_write_uncoded_frame(dev->output.afmt_ctx, 0, frame);
			assert(ret >= 0);
			frame = av_frame_alloc();  // av_interleaved_write takes onwership completely
			ret   = mfgraph_receive(&dev->output.agraph, frame);
		}
		assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
	} else {
		if (dev->output.vfmt_ctx == NULL)
			av_frame_free(&frame);
		else {
			// av_interleaved_write_uncoded_frame not implemented for sdl,sdl2
			frame->display_picture_number = frame->coded_picture_number;
			int ret = av_interleaved_write_uncoded_frame(dev->output.vfmt_ctx, 0, frame);
			if (ret < 0) {
				char buf[AV_ERROR_MAX_STRING_SIZE];
				fprintf(stderr,
				        "failed to write to output video device: ret = %d (%s)\n",
				        ret,
				        av_make_error_string(buf, sizeof(buf), ret));
			}
		}
	}
}

void dev_close(struct Dev** dev) {
	if (*dev != NULL) {
		if ((*dev)->type == INPUT) {
			(*dev)->input.close_requested = true;
			pthread_join((*dev)->input.audio_worker_thread, NULL);
			pthread_join((*dev)->input.video_worker_thread, NULL);
		} else {
			av_write_trailer((*dev)->output.afmt_ctx);
			avformat_free_context((*dev)->output.afmt_ctx);
			mfgraph_free(&(*dev)->output.agraph);
			if ((*dev)->output.vfmt_ctx != NULL) {
				av_write_trailer((*dev)->output.vfmt_ctx);
				avformat_free_context((*dev)->output.vfmt_ctx);
			}
		}
		free(*dev);
		*dev = NULL;
	}
}
