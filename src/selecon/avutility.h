#pragma once

#include <libavcodec/packet.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>

static void av_frame_dump(FILE* fp, struct AVFrame* frame) {
  if (frame->nb_samples != 0) {
    fprintf(fp, "nb_samples=%d\n", frame->nb_samples);
    fprintf(fp, "sample_fmt=%s\n", av_get_sample_fmt_name(frame->format));
    fprintf(fp, "sample_rate=%d\n", frame->sample_rate);
    char ch_layout[64];
    av_channel_layout_describe(&frame->ch_layout, ch_layout, sizeof(ch_layout));
    fprintf(fp, "ch_layout=%s\n", ch_layout);
  } else {
    fprintf(fp, "width=%d\n", frame->width);
    fprintf(fp, "height=%d\n", frame->height);
    fprintf(fp, "pix_fmt=%s\n", av_get_pix_fmt_name(frame->format));
  }
  fprintf(fp, "time_base=%d/%d\n", frame->time_base.num, frame->time_base.den);
  fprintf(fp, "pts=%ld\n", frame->pts);
  fprintf(fp, "pts_time=%lf\n", frame->pts * av_q2d(frame->time_base));
  fprintf(fp, "dts=%ld\n", frame->pkt_dts);
  fprintf(fp, "dts_time=%lf\n", frame->pkt_dts * av_q2d(frame->time_base));
}

static size_t av_packet_serialize(uint8_t* buffer, struct AVPacket *pkt) {
  size_t size = 2 * sizeof(int64_t) + 3 * sizeof(int) + pkt->size;
  for (int i = 0; i < pkt->side_data_elems; ++i)
    size += sizeof(enum AVPacketSideDataType) + sizeof(size_t) + pkt->side_data[i].size;
  if (buffer != NULL) {
    memcpy(buffer, &pkt->size, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, &pkt->pts, sizeof(int64_t));
    buffer += sizeof(int64_t);
    memcpy(buffer, &pkt->dts, sizeof(int64_t));
    buffer += sizeof(int64_t);
    memcpy(buffer, &pkt->flags, sizeof(int));
    buffer += sizeof(int);
    memcpy(buffer, &pkt->side_data_elems, sizeof(int));
    buffer += sizeof(int);
    for (int i = 0; i < pkt->side_data_elems; ++i) {
      memcpy(buffer, &pkt->side_data[i].type, sizeof(enum AVPacketSideDataType));
      buffer += sizeof(enum AVPacketSideDataType);
      memcpy(buffer, &pkt->side_data[i].size, sizeof(size_t));
      buffer += sizeof(size_t);
      memcpy(buffer, pkt->side_data[i].data, pkt->side_data[i].size);
      buffer += pkt->side_data[i].size;
    }
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
  int side_data_elems;
  memcpy(&side_data_elems, buffer, sizeof(int));
  buffer += sizeof(int);
  for (int i = 0; i < side_data_elems; ++i) {
    enum AVPacketSideDataType type;
    memcpy(&type, buffer, sizeof(type));
    buffer += sizeof(type);
    size_t size;
    memcpy(&size, buffer, sizeof(size_t));
    buffer += sizeof(size_t);
    uint8_t *data = av_packet_new_side_data(pkt, type, size);
    if (data == NULL) {
      fprintf(stderr, "failed to allocate packet side data\n");
      exit(-1);
    }
    memcpy(data, buffer, size);
    buffer += size;
  }
  memcpy(pkt->data, buffer, pkt->size);
  return pkt;
}
