#include "dump.h"

#include <assert.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <stdlib.h>

struct PacketDump {
	// char* filename;
	struct AVFormatContext* fmt_ctx;
};

struct PacketDump* pdump_create(const char* filename, size_t streams) {
	struct PacketDump* pdump = calloc(1, sizeof(struct PacketDump));

	avformat_alloc_output_context2(&pdump->fmt_ctx, NULL, NULL, filename);
	if (!pdump->fmt_ctx) {
		perror("avformat_alloc_output_context2");
		free(pdump);
		return NULL;
	}
	for (size_t i = 0; i < streams; ++i) {
		struct AVStream* stream = avformat_new_stream(pdump->fmt_ctx, NULL);
		assert(stream != NULL);
	}
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

void pdump_dump(struct PacketDump* pdump, size_t stream_index, struct AVPacket* packet) {
	packet->pos = -1;
	av_interleaved_write_frame(pdump->fmt_ctx, packet);
	av_packet_unref(packet);
}
