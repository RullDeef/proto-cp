#pragma once

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>

#include "error.h"

struct MediaFilterGraph {
	enum AVMediaType type;
	struct AVFilterGraph *filter_graph;
	struct AVFilterContext *filter_src;
	struct AVFilterContext *filter_sink;
	struct AVAudioFifo *audio_fifo;
	size_t audio_frame_size;
};

enum SError mfgraph_init(struct MediaFilterGraph *mf_graph, enum AVMediaType type);

void mfgraph_free(struct MediaFilterGraph *mf_graph);

int mfgraph_send(struct MediaFilterGraph *mf_graph,
                 struct AVCodecContext *codec_ctx,
                 struct AVFrame *frame);

int mfgraph_receive(struct MediaFilterGraph *mf_graph, struct AVFrame *frame);
