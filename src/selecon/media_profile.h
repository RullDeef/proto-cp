#pragma once

#include <libavcodec/codec_id.h>
#include <libavutil/samplefmt.h>

struct SAudioProfile {
	enum AVCodecID codec_id;
	enum AVSampleFormat sample_fmt;
	int sample_rate;
	int nb_channels;
	int frame_size;
};

struct SVideoProfile {
	enum AVCodecID codec_id;
	enum AVPixelFormat pixel_fmt;
	int width;
	int height;
	int framerate;
};

void register_media_profiles(void);
