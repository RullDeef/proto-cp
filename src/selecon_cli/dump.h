#pragma once

#include <libavcodec/packet.h>
#include <stddef.h>

struct PacketDump;

struct PacketDump* pdump_create(const char* filename, size_t num_streams);

void pdump_free(struct PacketDump** pdump);

void pdump_dump(struct PacketDump* pdump, size_t stream_index, struct AVPacket* packet);
