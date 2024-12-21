#include "dev_io.h"

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <pthread.h>

#include "config.h"
#include "media_filters.h"
#include "selecon.h"

enum DevPairType { INPUT, OUTPUT };

struct DevPairInput {
	bool close_requested;
	struct SContext* context;
	char* adev_name;
	char* vdev_name;
	pthread_t worker_thread;
	pthread_mutex_t mutex;
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
		fprintf(stderr, "output video device '%s' not found\n", dev_name);
	}
	return NULL;
}

static void* input_worker(void* arg) {
	struct DevPairInput* dev         = arg;
	const AVInputFormat* afmt        = find_input_audio_fmt(dev->adev_name);
	const AVInputFormat* vfmt        = find_input_video_fmt(dev->vdev_name);
	struct AVFormatContext* afmt_ctx = NULL;
	if (avformat_open_input(&afmt_ctx, NULL, afmt, NULL) < 0) {
		fprintf(stderr, "failed to open audio input\n");
		return NULL;
	}
	// TODO: complete
	return NULL;
}

// allocates output streams for each non-NULL dev_name and starts sending frames to streams
struct Dev* dev_create_src(struct SContext* context, const char* adev_name, const char* vdev_name) {
	struct Dev* dev      = calloc(1, sizeof(struct Dev));
	dev->type            = INPUT;
	dev->input.context   = context;
	dev->input.adev_name = adev_name == NULL ? NULL : strdup(adev_name);
	dev->input.vdev_name = vdev_name == NULL ? NULL : strdup(vdev_name);
	pthread_mutex_init(&dev->input.mutex, NULL);
	pthread_create(&dev->input.worker_thread, NULL, input_worker, &dev->input);
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
	av_channel_layout_default(&astream->codecpar->ch_layout, 2);  // TODO: fix for stereo
	// commit
	ret = avformat_write_header(dev->output.afmt_ctx, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to write audio header: ret = %d\n", ret);
		goto cleanup;
	}
	// create audio filter graph
	mfgraph_init_audio(&dev->output.agraph, AV_SAMPLE_FMT_S16, 48000, 2, 1024);
	return dev;
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
		// TODO: implement
	}
}

void dev_close(struct Dev** dev) {
	if (*dev != NULL) {
		if ((*dev)->type == INPUT) {
			// TODO: implement
		} else {
			dev_push_frame(*dev, AVMEDIA_TYPE_AUDIO, NULL);
			dev_push_frame(*dev, AVMEDIA_TYPE_VIDEO, NULL);
			av_write_trailer((*dev)->output.afmt_ctx);
			avformat_free_context((*dev)->output.afmt_ctx);
			mfgraph_free(&(*dev)->output.agraph);
		}
		free(*dev);
		*dev = NULL;
	}
}
