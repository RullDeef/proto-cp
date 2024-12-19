#include "dump.h"

#include <assert.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <stdlib.h>
#include "selecon.h"
#include "avutility.h"
#include "config.h"

struct PacketDump {
	//char* filename;
	struct AVFormatContext* fmt_ctx;

  struct AVStream* audio_stream;
  struct AVCodecContext* acodec_ctx;

  //struct AVStream* video_stream;
};

static struct AVStream* create_audio_stream(struct AVFormatContext* fmt_ctx, struct AVCodecContext **codec_ctx) {
  struct AVStream* stream = avformat_new_stream(fmt_ctx, NULL);
  assert(stream != NULL);

  // create codec context (use raw PCM encoder)
  const struct AVCodec* codec = avcodec_find_encoder(SELECON_DEFAULT_AUDIO_CODEC_ID);
  assert(codec != NULL);

  *codec_ctx = avcodec_alloc_context3(codec);
  assert(*codec_ctx != NULL);

  (*codec_ctx)->sample_fmt = SELECON_DEFAULT_AUDIO_SAMPLE_FMT;
  (*codec_ctx)->sample_rate = SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
  (*codec_ctx)->frame_size = SELECON_DEFAULT_AUDIO_FRAME_SIZE;
  (*codec_ctx)->bit_rate = 96000;
  av_channel_layout_default(&(*codec_ctx)->ch_layout, SELECON_DEFAULT_AUDIO_CHANNELS);

  if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    (*codec_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  int ret = avcodec_open2(*codec_ctx, codec, NULL);
  assert(ret == 0);
  assert((*codec_ctx)->sample_fmt == SELECON_DEFAULT_AUDIO_SAMPLE_FMT);
  assert((*codec_ctx)->sample_rate == SELECON_DEFAULT_AUDIO_SAMPLE_RATE);
  assert((*codec_ctx)->ch_layout.nb_channels == SELECON_DEFAULT_AUDIO_CHANNELS);

  avcodec_parameters_from_context(stream->codecpar, *codec_ctx);
  stream->time_base = av_make_q(1, 1000);
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
  pdump->audio_stream = create_audio_stream(pdump->fmt_ctx, &pdump->acodec_ctx);
	if (!(pdump->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
		int ret = avio_open(&pdump->fmt_ctx->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			perror("avio_open");
			assert(0);
		}
	}
  av_dump_format(pdump->fmt_ctx, 0, filename, 1);
	int ret = avformat_write_header(pdump->fmt_ctx, NULL);
	assert(ret >= 0);
	return pdump;
}

static void pdump_dump_audio(struct PacketDump *pdump, struct AVFrame *frame) {
  if (frame != NULL) {
    printf("pdump_dump_audio:\n");
    av_frame_dump(stdout, frame);
  }
  int ret = avcodec_send_frame(pdump->acodec_ctx, frame);
  assert(ret == 0);
  AVPacket* packet = av_packet_alloc();
  ret = avcodec_receive_packet(pdump->acodec_ctx, packet);
  while (ret == 0) {
    packet->stream_index = pdump->audio_stream->index;
    av_packet_rescale_ts(packet,
                         pdump->acodec_ctx->time_base,
                         pdump->audio_stream->time_base);
  	av_interleaved_write_frame(pdump->fmt_ctx, packet);
    ret = avcodec_receive_packet(pdump->acodec_ctx, packet);
  }
  assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
	av_packet_unref(packet);
}

void pdump_free(struct PacketDump** pdump) {
	if (*pdump != NULL) {
    pdump_dump_audio(*pdump, NULL); // flush
		int ret = av_write_trailer((*pdump)->fmt_ctx);
    if (ret < 0) {
      printf("failed to write trailer! err = %d\n", ret);
    }
    avcodec_free_context(&(*pdump)->acodec_ctx);
		*pdump = NULL;
	}
}

void pdump_dump(struct PacketDump* pdump, enum AVMediaType mtype, struct AVFrame* frame) {
  if (mtype == AVMEDIA_TYPE_AUDIO)
    pdump_dump_audio(pdump, frame);
  else
    assert(0);
}
