#pragma once

#include "selecon.h"
#include "stypes.h"

struct StatFile;
struct AVFrame;
enum AVMediaType;

void statfile_open(struct StatFile** sf, const char* name);

void statfile_close(struct StatFile** sf);

void statfile_mark_arrived(struct StatFile* sf,
                           struct SContext* ctx,
                           part_id_t part_id,
                           enum AVMediaType mtype,
                           struct AVFrame* frame);
