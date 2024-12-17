#include "dump.h"

#include <assert.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <stdlib.h>
#include "selecon.h"
#include "config.h"

struct PacketDump {
	//char* filename;
	struct AVFormatContext* fmt_ctx;
  struct AVStream* audio_stream;
  //struct AVStream* video_stream;
};

static struct AVStream* create_audio_stream(struct AVFormatContext* fmt_ctx) {
  struct AVStream* stream = avformat_new_stream(fmt_ctx, NULL);
  assert(stream != NULL);
  stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
  stream->codecpar->codec_id = SELECON_DEFAULT_AUDIO_CODEC_ID;
  stream->codecpar->format = SELECON_DEFAULT_AUDIO_SAMPLE_FMT;
  stream->codecpar->sample_rate = SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
  av_channel_layout_default(&stream->codecpar->ch_layout, SELECON_DEFAULT_AUDIO_CHANNELS);
  return stream;
}

struct PacketDump* pdump_create(const char* filename) {
	struct PacketDump* pdump = calloc(1, sizeof(struct PacketDump));

	avformat_alloc_output_context2(&pdump->fmt_ctx, NULL, NULL, filename);
	if (!pdump->fmt_ctx) {
		perror("avformat_alloc_output_context2");
		free(pdump);
		return NULL;
	}
  pdump->audio_stream = create_audio_stream(pdump->fmt_ctx);
	if (!(pdump->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&pdump->fmt_ctx->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			perror("avio_open");
			assert(0);
		}
	}
	int ret = avformat_write_header(pdump->fmt_ctx, NULL);
	assert(ret >= 0);
	return pdump;
}

void pdump_free(struct PacketDump** pdump) {
	if (*pdump != NULL) {
		av_write_trailer((*pdump)->fmt_ctx);
		*pdump = NULL;
	}
}

void pdump_dump(struct PacketDump* pdump, enum AVMediaType mtype, struct AVPacket* packet) {
	packet->pos = -1;
  if (mtype == AVMEDIA_TYPE_AUDIO)
    packet->stream_index = pdump->audio_stream->index;
  else
    assert(0);
	av_interleaved_write_frame(pdump->fmt_ctx, packet);
	av_packet_unref(packet);
}
