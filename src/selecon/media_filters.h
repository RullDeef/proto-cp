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
	union {
		struct {
			enum AVSampleFormat sample_fmt;
			int sample_rate;
			int nb_channels;
			int frame_size;
		};
		struct {
			enum AVPixelFormat pixel_fmt;
			int width;
			int height;
		};
	};
	struct AVFilterGraph *filter_graph;
	struct AVFilterContext *filter_src;
	struct AVFilterContext *filter_sink;
	struct AVAudioFifo *audio_fifo;
};

void mfgraph_init_audio(struct MediaFilterGraph *mf_graph,
                        enum AVSampleFormat sample_fmt,
                        int sample_rate,
                        int nb_channels,
                        int frame_size);

void mfgraph_init_video(struct MediaFilterGraph *mf_graph,
                        enum AVPixelFormat pixel_fmt,
                        int width,
                        int height);

void mfgraph_free(struct MediaFilterGraph *mf_graph);

// frame ownership remains with caller
int mfgraph_send(struct MediaFilterGraph *mf_graph, struct AVFrame *frame);

// frame must be allocated by caller and will be filled with reference-counted buffers
int mfgraph_receive(struct MediaFilterGraph *mf_graph, struct AVFrame *frame);
