#pragma once

#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <stddef.h>

struct PacketDump;

struct PacketDump* pdump_create(const char* filename);

void pdump_free(struct PacketDump** pdump);

void pdump_dump(struct PacketDump* pdump, enum AVMediaType mtype, struct AVPacket* packet);
