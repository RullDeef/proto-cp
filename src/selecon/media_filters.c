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

enum SError build_filter_graph_audio(struct MediaFilterGraph *mf_graph,
                                     struct AVCodecContext *codec_ctx,
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

	av_channel_layout_describe(&codec_ctx->ch_layout, ch_layout, sizeof(ch_layout));
	snprintf(args,
	         sizeof(args),
	         "sample_fmts=%s:sample_rates=%d:channel_layouts=%s",
	         av_get_sample_fmt_name(codec_ctx->sample_fmt),
	         codec_ctx->sample_rate,
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

	char *dump = avfilter_graph_dump(mf_graph->filter_graph, NULL);
	fprintf(stdout, "initialied audio filter graph: %s\n", dump);
	free(dump);

	mf_graph->audio_fifo = av_audio_fifo_alloc(
	    codec_ctx->sample_fmt, codec_ctx->ch_layout.nb_channels, 2 * codec_ctx->frame_size);
	printf("av audio fifo buffer: %p frame size: %d nb channels: %d sample_fmt: %d\n",
	       mf_graph->audio_fifo,
	       codec_ctx->frame_size,
	       codec_ctx->ch_layout.nb_channels,
	       codec_ctx->sample_fmt);
	assert(mf_graph->audio_fifo != NULL);
	mf_graph->audio_frame_size = codec_ctx->frame_size;
	return SELECON_OK;
}

enum SError build_filter_graph_video(struct MediaFilterGraph *mf_graph,
                                     struct AVCodecContext *codec_ctx,
                                     struct AVFrame *frame) {
	const struct AVFilter *buffer_src  = avfilter_get_by_name("buffer");
	const struct AVFilter *scale       = avfilter_get_by_name("scale");
	const struct AVFilter *buffer_sink = avfilter_get_by_name("buffersink");
	char args[512];

	mf_graph->filter_graph = avfilter_graph_alloc();
	assert(mf_graph->filter_graph != NULL);

	mf_graph->filter_src = avfilter_graph_alloc_filter(mf_graph->filter_graph, buffer_src, "in");
	assert(mf_graph->filter_src != NULL);

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
		fprintf(stderr, "failed to init video source filter: err = %d\n", err);
		assert(0);
	}

	struct AVFilterContext *scale_ctx =
	    avfilter_graph_alloc_filter(mf_graph->filter_graph, scale, "scale");

	snprintf(args,
	         sizeof(args),
	         "width=%d:height=%d",
	         SELECON_DEFAULT_VIDEO_WIDTH,
	         SELECON_DEFAULT_VIDEO_HEIGHT);
	err = avfilter_init_str(scale_ctx, args);
	if (err != 0) {
		fprintf(stderr, "failed to init video filter: err = %d\n", err);
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

	char *dump = avfilter_graph_dump(mf_graph->filter_graph, NULL);
	fprintf(stdout, "initialied video filter graph: %s\n", dump);
	free(dump);

	return SELECON_OK;
}

enum SError mfgraph_init(struct MediaFilterGraph *mf_graph, enum AVMediaType type) {
	mf_graph->type         = type;
	mf_graph->filter_graph = NULL;
	mf_graph->filter_src   = NULL;
	mf_graph->filter_sink  = NULL;
	mf_graph->audio_fifo   = NULL;
	return SELECON_OK;
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

int mfgraph_send(struct MediaFilterGraph *mf_graph,
                 struct AVCodecContext *codec_ctx,
                 struct AVFrame *frame) {
	// lazy initialization
	if (mf_graph->filter_graph == NULL) {
		enum SError err = mf_graph->type == AVMEDIA_TYPE_AUDIO
		                      ? build_filter_graph_audio(mf_graph, codec_ctx, frame)
		                      : build_filter_graph_video(mf_graph, codec_ctx, frame);
		if (err != SELECON_OK) {
			fprintf(stderr, "failed to create filter graph: err = %s\n", serror_str(err));
			av_frame_free(&frame);
			return -1;
		}
	}
	int ret = av_buffersrc_add_frame(mf_graph->filter_src, frame);
	if (ret < 0) {
		perror("av_buffersrc_add_frame");
		return ret;
	}
	return 0;
}

static int mfgraph_receive_audio(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	int ret;

	// Ensure we have enough samples before reading from the buffer
	while (av_audio_fifo_size(mf_graph->audio_fifo) < mf_graph->audio_frame_size) {
		// Attempt to get a frame from the filter sink
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
	ret =
	    av_audio_fifo_read(mf_graph->audio_fifo, (void **)frame->data, mf_graph->audio_frame_size);
	if (ret < 0) {
		perror("Error reading from audio FIFO");
		return ret;  // Return the error
	}

	// Adjust frame metadata accordingly (e.g., set nb_samples)
	frame->nb_samples = mf_graph->audio_frame_size;  // Properly set the size for the next frame

	return 0;  // Return success
}

static int mfgraph_receive_video(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	int ret = av_buffersink_get_frame(mf_graph->filter_sink, frame);
	if (ret < 0 && ret != AVERROR(EAGAIN))
		perror("Error getting frame from buffer sink");
	return ret;
}

int mfgraph_receive(struct MediaFilterGraph *mf_graph, struct AVFrame *frame) {
	switch (mf_graph->type) {
		case AVMEDIA_TYPE_AUDIO: return mfgraph_receive_audio(mf_graph, frame);
		case AVMEDIA_TYPE_VIDEO: return mfgraph_receive_video(mf_graph, frame);
		default:                 fprintf(stderr, "unrecognized media graph type: %d\n", mf_graph->type); return -1;
	}
}
