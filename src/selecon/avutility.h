#pragma once

#include <libavcodec/codec_id.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void av_frame_dump(FILE* fp, struct AVFrame* frame);

size_t av_packet_serialize(uint8_t* buffer, struct AVPacket* pkt);

struct AVPacket* av_packet_deserialize(uint8_t* buffer);

bool check_audio_codec(enum AVCodecID id,
                       enum AVSampleFormat sample_fmt,
                       int sample_rate,
                       int nb_channels,
                       int frame_size);

bool check_video_codec(
    enum AVCodecID id, enum AVPixelFormat pixel_fmt, int width, int height, int framerate);
