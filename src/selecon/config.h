#pragma once

#include "version.h"

#define SELECON_DEFAULT_LISTEN_ADDR "127.0.0.1"
#define SELECON_DEFAULT_LISTEN_PORT 11235
#define SELECON_DEFAULT_LISTEN_PORT_STR STRINGIFY(SELECON_DEFAULT_LISTEN_PORT)

#define SELECON_DEFAULT_PART_NAME "user0"

#define SELECON_DEFAULT_LISTEN_BUF 5
#define SELECON_DEFAULT_SNDRCV_TIMEOUT 500     // ms
#define SELECON_DEFAULT_SECURE_TIMEOUT 5000    // ms
#define SELECON_DEFAULT_REENTER_TIMEOUT 30000  // ms

// #define SELECON_USE_SECURE_CONNECTION

// TODO: add check at context creation that selected codec supports our defaults
#define SELECON_DEFAULT_AUDIO_CODEC_ID AV_CODEC_ID_AAC
#define SELECON_DEFAULT_AUDIO_SAMPLE_RATE 48000
#define SELECON_DEFAULT_AUDIO_CHANNELS 2
#define SELECON_DEFAULT_AUDIO_SAMPLE_FMT AV_SAMPLE_FMT_FLTP
#define SELECON_DEFAULT_AUDIO_FRAME_SIZE 1024  // samples

#define SELECON_DEFAULT_VIDEO_CODEC_ID AV_CODEC_ID_H264
#define SELECON_DEFAULT_VIDEO_PIXEL_FMT AV_PIX_FMT_YUV420P
#define SELECON_DEFAULT_VIDEO_WIDTH 320
#define SELECON_DEFAULT_VIDEO_HEIGHT 180
#define SELECON_DEFAULT_VIDEO_FPS 30
