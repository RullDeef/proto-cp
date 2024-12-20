#include "dump.h"

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <stdlib.h>

#include "config.h"

struct PacketDump {
	struct AVFormatContext* fmt_ctx;
	struct AVStream* audio_stream;
	struct AVCodecContext* acodec_ctx;
	struct AVStream* video_stream;
	struct AVCodecContext* vcodec_ctx;
};

struct PacketDumpMap {
	struct PacketDump** pdumps;
	part_id_t* ids;

	const char* filename_base;
	size_t count;
};

static struct AVStream* create_audio_stream(struct AVFormatContext* fmt_ctx,
                                            struct AVCodecContext** codec_ctx) {
	struct AVStream* stream = avformat_new_stream(fmt_ctx, NULL);
	assert(stream != NULL);

	const struct AVCodec* codec = avcodec_find_encoder(SELECON_DEFAULT_AUDIO_CODEC_ID);
	assert(codec != NULL);

	*codec_ctx = avcodec_alloc_context3(codec);
	assert(*codec_ctx != NULL);

	(*codec_ctx)->sample_fmt  = SELECON_DEFAULT_AUDIO_SAMPLE_FMT;
	(*codec_ctx)->sample_rate = SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
	(*codec_ctx)->frame_size  = SELECON_DEFAULT_AUDIO_FRAME_SIZE;
	(*codec_ctx)->bit_rate    = 96000;
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

static struct AVStream* create_video_stream(struct AVFormatContext* fmt_ctx,
                                            struct AVCodecContext** codec_ctx) {
	struct AVStream* stream = avformat_new_stream(fmt_ctx, NULL);
	assert(stream != NULL);

	const struct AVCodec* codec = avcodec_find_encoder(SELECON_DEFAULT_VIDEO_CODEC_ID);
	assert(codec != NULL);

	*codec_ctx = avcodec_alloc_context3(codec);
	assert(*codec_ctx != NULL);

	(*codec_ctx)->pix_fmt   = SELECON_DEFAULT_VIDEO_PIXEL_FMT;
	(*codec_ctx)->width     = SELECON_DEFAULT_VIDEO_WIDTH;
	(*codec_ctx)->height    = SELECON_DEFAULT_VIDEO_HEIGHT;
	(*codec_ctx)->framerate = av_make_q(1, SELECON_DEFAULT_VIDEO_FPS);
	(*codec_ctx)->time_base = av_make_q(1, 1000);

	if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		(*codec_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	int ret = avcodec_open2(*codec_ctx, codec, NULL);
	assert(ret == 0);
	assert((*codec_ctx)->pix_fmt == SELECON_DEFAULT_VIDEO_PIXEL_FMT);
	assert((*codec_ctx)->width == SELECON_DEFAULT_VIDEO_WIDTH);
	assert((*codec_ctx)->height == SELECON_DEFAULT_VIDEO_HEIGHT);
	assert((*codec_ctx)->framerate.den == SELECON_DEFAULT_VIDEO_FPS);

	avcodec_parameters_from_context(stream->codecpar, *codec_ctx);
	stream->r_frame_rate = av_make_q(SELECON_DEFAULT_VIDEO_FPS, 1);
	return stream;
}

static struct PacketDump* pdump_create(const char* filename) {
	struct PacketDump* pdump = calloc(1, sizeof(struct PacketDump));

	avformat_alloc_output_context2(&pdump->fmt_ctx, NULL, NULL, filename);
	if (!pdump->fmt_ctx) {
		perror("avformat_alloc_output_context2");
		free(pdump);
		return NULL;
	}
	pdump->audio_stream = create_audio_stream(pdump->fmt_ctx, &pdump->acodec_ctx);
	pdump->video_stream = create_video_stream(pdump->fmt_ctx, &pdump->vcodec_ctx);
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

static void pdump_dump_common(struct AVFormatContext* fmt_ctx,
                              struct AVStream* stream,
                              struct AVCodecContext* codec_ctx,
                              struct AVFrame* frame) {
	int ret = avcodec_send_frame(codec_ctx, frame);
	if (ret != 0) {
		fprintf(stderr, "failed to send frame to codec: ret = %d\n", ret);
		return;
	}
	AVPacket* packet = av_packet_alloc();
	ret              = avcodec_receive_packet(codec_ctx, packet);
	while (ret == 0) {
		packet->stream_index = stream->index;
		av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
		av_interleaved_write_frame(fmt_ctx, packet);
		ret = avcodec_receive_packet(codec_ctx, packet);
	}
	assert(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
	av_packet_unref(packet);
}

static void pdump_dump_audio(struct PacketDump* pdump, struct AVFrame* frame) {
	pdump_dump_common(pdump->fmt_ctx, pdump->audio_stream, pdump->acodec_ctx, frame);
}

static void pdump_dump_video(struct PacketDump* pdump, struct AVFrame* frame) {
	if (frame != NULL) {
		frame->pict_type = AV_PICTURE_TYPE_NONE;  // reset picture type, let encoder decide
		frame->pts       = av_rescale(frame->pts, 1000, 15360);  // dirty hack for timestamps
		frame->pkt_dts   = AV_NOPTS_VALUE;
	}
	pdump_dump_common(pdump->fmt_ctx, pdump->video_stream, pdump->vcodec_ctx, frame);
}

static void pdump_free(struct PacketDump** pdump) {
	if (*pdump != NULL) {
		// flush audio/video codecs
		pdump_dump_audio(*pdump, NULL);
		pdump_dump_video(*pdump, NULL);
		int ret = av_write_trailer((*pdump)->fmt_ctx);
		if (ret < 0) {
			printf("failed to write trailer! err = %d\n", ret);
		}
		avcodec_free_context(&(*pdump)->acodec_ctx);
		avcodec_free_context(&(*pdump)->vcodec_ctx);
		*pdump = NULL;
	}
}

static void pdump_dump(struct PacketDump* pdump, enum AVMediaType mtype, struct AVFrame* frame) {
	switch (mtype) {
		case AVMEDIA_TYPE_AUDIO: return pdump_dump_audio(pdump, frame);
		case AVMEDIA_TYPE_VIDEO: return pdump_dump_video(pdump, frame);
		default:                 return assert(0);
	}
}

// packet dump map api ------

struct PacketDumpMap* pdmap_create(const char* filename_base) {
	struct PacketDumpMap* pdmap = calloc(1, sizeof(struct PacketDumpMap));
	if (pdmap == NULL) {
		perror("calloc");
		return NULL;
	}
	pdmap->filename_base = filename_base;
	return pdmap;
}

void pdmap_free(struct PacketDumpMap** pdmap) {
	if ((*pdmap)->count == 0)
		return;
	for (size_t i = 0; i < (*pdmap)->count; ++i) pdump_free(&(*pdmap)->pdumps[i]);
	free((*pdmap)->pdumps);
	free((*pdmap)->ids);
	free(*pdmap);
	*pdmap = NULL;
}

void pdmap_dump(struct PacketDumpMap* pdmap,
                part_id_t part_id,
                enum AVMediaType mtype,
                struct AVFrame* frame) {
	for (size_t i = 0; i < pdmap->count; ++i)
		if (pdmap->ids[i] == part_id)
			return pdump_dump(pdmap->pdumps[i], mtype, frame);
	// dumper not found - create new one
	pdmap->count++;
	pdmap->ids    = reallocarray(pdmap->ids, pdmap->count, sizeof(part_id_t));
	pdmap->pdumps = reallocarray(pdmap->pdumps, pdmap->count, sizeof(struct PacketDump));
	pdmap->ids[pdmap->count - 1] = part_id;
	char filename[256];
	snprintf(filename, sizeof(filename), "%s_%llu.mkv", pdmap->filename_base, part_id);
	pdmap->pdumps[pdmap->count - 1] = pdump_create(filename);
	pdump_dump(pdmap->pdumps[pdmap->count - 1], mtype, frame);
}
