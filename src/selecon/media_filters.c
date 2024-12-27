#include "media_filters.h"

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>

#include "avutility.h"
#include "config.h"
#include "error.h"

static void prepare_frame(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	av_frame_unref(frame);
	if (mf_graph->type == AVMEDIA_TYPE_AUDIO) {
		frame->format      = mf_graph->sample_fmt;
		frame->sample_rate = mf_graph->sample_rate;
		av_channel_layout_default(&frame->ch_layout, mf_graph->nb_channels);
		frame->nb_samples = mf_graph->frame_size;
	} else {
		frame->format = mf_graph->pixel_fmt;
		frame->width  = mf_graph->width;
		frame->height = mf_graph->height;
	}
	av_frame_get_buffer(frame, 0);
}

static enum SError build_filter_graph_audio(struct MediaFilterGraph *mf_graph,
                                            struct AVFrame *frame) {
	const struct AVFilter *buffer_src  = avfilter_get_by_name("abuffer");
	const struct AVFilter *format      = avfilter_get_by_name("aformat");
	const struct AVFilter *buffer_sink = avfilter_get_by_name("abuffersink");
	char ch_layout[64];
	char args[512];

	mf_graph->filter_graph = avfilter_graph_alloc();
	assert(mf_graph->filter_graph != NULL);

	mf_graph->filter_src = avfilter_graph_alloc_filter(mf_graph->filter_graph, buffer_src, "in");
	assert(mf_graph->filter_src != NULL);

	av_channel_layout_describe(&frame->ch_layout, ch_layout, sizeof(ch_layout));
	snprintf(args,
	         sizeof(args),
	         "sample_fmt=%s:sample_rate=%d:channel_layout=%s",
	         av_get_sample_fmt_name(frame->format),
	         frame->sample_rate,
	         ch_layout);
	int err = avfilter_init_str(mf_graph->filter_src, args);
	assert(err == 0);

	struct AVFilterContext *format_ctx =
	    avfilter_graph_alloc_filter(mf_graph->filter_graph, format, "aformat");

	struct AVChannelLayout layout;
	av_channel_layout_default(&layout, mf_graph->nb_channels);
	av_channel_layout_describe(&layout, ch_layout, sizeof(ch_layout));
	snprintf(args,
	         sizeof(args),
	         "sample_fmts=%s:sample_rates=%d:channel_layouts=%s",
	         av_get_sample_fmt_name(mf_graph->sample_fmt),
	         mf_graph->sample_rate,
	         ch_layout);
	err = avfilter_init_str(format_ctx, args);
	assert(err == 0);

	mf_graph->filter_sink = avfilter_graph_alloc_filter(mf_graph->filter_graph, buffer_sink, "out");
	err                   = avfilter_init_str(mf_graph->filter_sink, NULL);
	assert(err == 0);

	// connect filters
	err = avfilter_link(mf_graph->filter_src, 0, format_ctx, 0);
	assert(err == 0);

	err = avfilter_link(format_ctx, 0, mf_graph->filter_sink, 0);
	assert(err == 0);

	err = avfilter_graph_config(mf_graph->filter_graph, NULL);
	assert(err == 0);

	// char *dump = avfilter_graph_dump(mf_graph->filter_graph, NULL);
	// fprintf(stdout, "initialied audio filter graph: %s\n", dump);
	// free(dump);

	mf_graph->audio_fifo =
	    av_audio_fifo_alloc(mf_graph->sample_fmt, mf_graph->nb_channels, 2 * mf_graph->frame_size);
	// printf("av audio fifo buffer: %p frame size: %d nb channels: %d sample_fmt: %d\n",
	//        mf_graph->audio_fifo,
	//        mf_graph->frame_size,
	//        mf_graph->nb_channels,
	//        mf_graph->sample_fmt);
	assert(mf_graph->audio_fifo != NULL);
	return SELECON_OK;
}

enum SError build_filter_graph_video(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	const struct AVFilter *buffer_src  = avfilter_get_by_name("buffer");
	const struct AVFilter *scale       = avfilter_get_by_name("scale");
	const struct AVFilter *buffer_sink = avfilter_get_by_name("buffersink");
	char args[512];

	mf_graph->filter_graph = avfilter_graph_alloc();
	assert(mf_graph->filter_graph != NULL);

	mf_graph->filter_src = avfilter_graph_alloc_filter(mf_graph->filter_graph, buffer_src, "in");
	assert(mf_graph->filter_src != NULL);

	if (frame->time_base.num == 0)
		frame->time_base = av_make_q(1, 1000);

	snprintf(args,
	         sizeof(args),
	         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
	         frame->width,
	         frame->height,
	         frame->format,
	         frame->time_base.num,
	         frame->time_base.den,
	         frame->sample_aspect_ratio.num,
	         frame->sample_aspect_ratio.den);
	int err = avfilter_init_str(mf_graph->filter_src, args);
	if (err != 0) {
		fprintf(stderr, "failed to init video source filter: args=\"%s\" err = %d\n", args, err);
		assert(0);
	}

	struct AVFilterContext *scale_ctx =
	    avfilter_graph_alloc_filter(mf_graph->filter_graph, scale, "scale");

	snprintf(args, sizeof(args), "width=%d:height=%d", mf_graph->width, mf_graph->height);
	err = avfilter_init_str(scale_ctx, args);
	if (err != 0) {
		fprintf(stderr, "failed to init video filter: args=\"%s\" err = %d\n", args, err);
		assert(0);
	}

	mf_graph->filter_sink = avfilter_graph_alloc_filter(mf_graph->filter_graph, buffer_sink, "out");
	err                   = avfilter_init_str(mf_graph->filter_sink, NULL);
	if (err != 0) {
		fprintf(stderr, "failed to init video output filter: err = %d\n", err);
		assert(0);
	}

	// connect filters
	err = avfilter_link(mf_graph->filter_src, 0, scale_ctx, 0);
	assert(err == 0);

	err = avfilter_link(scale_ctx, 0, mf_graph->filter_sink, 0);
	assert(err == 0);

	err = avfilter_graph_config(mf_graph->filter_graph, NULL);
	assert(err == 0);

	// char *dump = avfilter_graph_dump(mf_graph->filter_graph, NULL);
	// fprintf(stdout, "initialied video filter graph: %s\n", dump);
	// free(dump);

	return SELECON_OK;
}

void mfgraph_init_audio(struct MediaFilterGraph *mf_graph,
                        enum AVSampleFormat sample_fmt,
                        int sample_rate,
                        int nb_channels,
                        int frame_size) {
	mf_graph->type         = AVMEDIA_TYPE_AUDIO;
	mf_graph->sample_fmt   = sample_fmt;
	mf_graph->sample_rate  = sample_rate;
	mf_graph->nb_channels  = nb_channels;
	mf_graph->frame_size   = frame_size;
	mf_graph->filter_graph = NULL;
	mf_graph->filter_src   = NULL;
	mf_graph->filter_sink  = NULL;
	mf_graph->audio_fifo   = NULL;
}

void mfgraph_init_video(struct MediaFilterGraph *mf_graph,
                        enum AVPixelFormat pixel_fmt,
                        int width,
                        int height) {
	mf_graph->type         = AVMEDIA_TYPE_VIDEO;
	mf_graph->pixel_fmt    = pixel_fmt;
	mf_graph->width        = width;
	mf_graph->height       = height;
	mf_graph->filter_graph = NULL;
	mf_graph->filter_src   = NULL;
	mf_graph->filter_sink  = NULL;
	mf_graph->audio_fifo   = NULL;
}

void mfgraph_free(struct MediaFilterGraph *mf_graph) {
	avfilter_graph_free(&mf_graph->filter_graph);
	mf_graph->filter_src  = NULL;
	mf_graph->filter_sink = NULL;
	if (mf_graph->audio_fifo != NULL) {
		av_audio_fifo_free(mf_graph->audio_fifo);
		mf_graph->audio_fifo = NULL;
	}
}

int mfgraph_send(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	// lazy initialization
	if (mf_graph->filter_graph == NULL) {
		enum SError err = mf_graph->type == AVMEDIA_TYPE_AUDIO
		                      ? build_filter_graph_audio(mf_graph, frame)
		                      : build_filter_graph_video(mf_graph, frame);
		if (err != SELECON_OK) {
			fprintf(stderr, "failed to create filter graph: err = %s\n", serror_str(err));
			return -1;
		}
	}
	int ret = av_buffersrc_write_frame(mf_graph->filter_src, frame);
	if (ret < 0) {
		perror("av_buffersrc_add_frame");
		return ret;
	}
	av_frame_unref(frame);
	return 0;
}

static int mfgraph_receive_audio(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	int ret;
	// Ensure we have enough samples before reading from the buffer
	while (av_audio_fifo_size(mf_graph->audio_fifo) < mf_graph->frame_size) {
		// Attempt to get a frame from the filter sink
		av_frame_unref(frame);
		ret = av_buffersink_get_frame(mf_graph->filter_sink, frame);
		if (ret < 0) {
			// Check if the error is a recoverable one
			if (ret == AVERROR(EAGAIN)) {
				return AVERROR(EAGAIN);  // Indicate that we need more frames
			}
			perror("Error getting frame from buffer sink");
			return ret;  // Return the error
		}

		// Write the frame's data into the audio FIFO
		av_audio_fifo_write(mf_graph->audio_fifo, (void **)frame->data, frame->nb_samples);
		// av_frame_unref(frame);  // Unreference frame to avoid memory issues
	}

	// Read samples from the FIFO into the provided frame (to fill it adequately)
	prepare_frame(mf_graph, frame);
	ret = av_audio_fifo_read(mf_graph->audio_fifo, (void **)frame->data, mf_graph->frame_size);
	if (ret < 0) {
		perror("Error reading from audio FIFO");
		return ret;
	}
	return 0;
}

static int mfgraph_receive_video(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	prepare_frame(mf_graph, frame);
	int ret = av_buffersink_get_frame(mf_graph->filter_sink, frame);
	if (ret < 0 && ret != AVERROR(EAGAIN))
		perror("Error getting frame from buffer sink");
	return ret;
}

int mfgraph_receive(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	switch (mf_graph->type) {
		case AVMEDIA_TYPE_AUDIO: return mfgraph_receive_audio(mf_graph, frame);
		case AVMEDIA_TYPE_VIDEO: return mfgraph_receive_video(mf_graph, frame);
		default: fprintf(stderr, "unrecognized media graph type: %d\n", mf_graph->type); return -1;
	}
}
