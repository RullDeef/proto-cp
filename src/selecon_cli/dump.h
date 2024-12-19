#pragma once

#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <stddef.h>
#include "selecon.h"

struct PacketDump;
struct PacketDumpMap;

struct PacketDumpMap* pdmap_create(const char* filename_base);
void pdmap_free(struct PacketDumpMap** pdmap);

void pdmap_dump(struct PacketDumpMap* pdmap, part_id_t part_id, enum AVMediaType mtype, struct AVFrame* frame);
