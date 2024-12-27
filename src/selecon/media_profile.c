#include "media_profile.h"

#include "avutility.h"

static bool media_profiles_registered = false;

// priority-ordered list of audio codec parameters
static struct SAudioProfile audio_profiles[] = {
    {AV_CODEC_ID_AAC, AV_SAMPLE_FMT_FLTP, 48000, 2, 1024},
    {AV_CODEC_ID_OPUS, AV_SAMPLE_FMT_S16, 48000, 2, 960},
    {AV_CODEC_ID_PCM_S16LE, AV_SAMPLE_FMT_S16, 16000, 1, 160},
    {AV_CODEC_ID_NONE},
};

// priority-ordered list of video codec parameters
static struct SVideoProfile video_profiles[] = {
    {AV_CODEC_ID_H264, AV_PIX_FMT_YUV420P, 320, 180, 30},
    {AV_CODEC_ID_VP8, AV_PIX_FMT_YUV420P, 320, 180, 30},
    {AV_CODEC_ID_NONE},
};

void register_media_profiles(void) {
	if (!media_profiles_registered) {
		for (struct SAudioProfile* p = audio_profiles; p->codec_id != AV_CODEC_ID_NONE; ++p) {
			if (!check_audio_codec(
			        p->codec_id, p->sample_fmt, p->sample_rate, p->nb_channels, p->frame_size)) {
				const int n = sizeof(audio_profiles) / sizeof(audio_profiles[0]);
				memcpy(p, p + 1, (char*)(audio_profiles + n) - (char*)(p + 1));
				--p;
			}
		}
		for (struct SVideoProfile* p = video_profiles; p->codec_id != AV_CODEC_ID_NONE; ++p) {
			if (!check_video_codec(p->codec_id, p->pixel_fmt, p->width, p->height, p->framerate)) {
				const int n = sizeof(video_profiles) / sizeof(video_profiles[0]);
				memcpy(p, p + 1, (char*)(video_profiles + n) - (char*)(p + 1));
				--p;
			}
		}
		media_profiles_registered = true;
	}
}
