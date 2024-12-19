#pragma once

#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <stdio.h>

static void av_frame_dump(FILE* fp, struct AVFrame* frame) {
	fprintf(fp, "nb_samples=%d\n", frame->nb_samples);
	fprintf(fp, "sample_fmt=%s\n", av_get_sample_fmt_name(frame->format));
	fprintf(fp, "sample_rate=%d\n", frame->sample_rate);
	fprintf(fp, "time_base=%d/%d\n", frame->time_base.num, frame->time_base.den);
	char ch_layout[64];
	av_channel_layout_describe(&frame->ch_layout, ch_layout, sizeof(ch_layout));
	fprintf(fp, "ch_layout=%s\n", ch_layout);
	fprintf(fp, "pts=%ld\n", frame->pts);
	fprintf(fp, "dts=%ld\n", frame->pkt_dts);
}

static size_t av_packet_serialize(uint8_t* buffer, struct AVPacket *pkt) {
  size_t size = 2 * sizeof(int64_t) + 2 * sizeof(int) + pkt->size;
  if (buffer != NULL) {
    memcpy(buffer, &pkt->size, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, &pkt->pts, sizeof(int64_t));
    buffer += sizeof(int64_t);
    memcpy(buffer, &pkt->dts, sizeof(int64_t));
    buffer += sizeof(int64_t);
    memcpy(buffer, &pkt->flags, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, pkt->data, pkt->size);
  }
  return size;
}

static struct AVPacket* av_packet_deserialize(uint8_t* buffer) {
  struct AVPacket *pkt = av_packet_alloc();
  if (!pkt)
    return NULL;
  memcpy(&pkt->size, buffer, sizeof(int));
  buffer += sizeof(int);
  pkt->data = (uint8_t*)av_malloc(pkt->size);
  if (!pkt->data) {
    av_packet_free(&pkt);
    return NULL; // Memory allocation failed
  }
  memcpy(&pkt->pts, buffer, sizeof(int64_t));
  buffer += sizeof(int64_t);
  memcpy(&pkt->dts, buffer, sizeof(int64_t));
  buffer += sizeof(int64_t);
  memcpy(&pkt->flags, buffer, sizeof(int));
  buffer += sizeof(int);
  memcpy(pkt->data, buffer, pkt->size);
  return pkt;
}
