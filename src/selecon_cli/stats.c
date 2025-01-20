#include "stats.h"

#include <libavutil/avutil.h>
#include <stdio.h>
#include <stdlib.h>

#include "stime.h"

struct StatFile {
	FILE* file;
};

void statfile_open(struct StatFile** sf, const char* name) {
	*sf         = malloc(sizeof(struct StatFile));
	(*sf)->file = fopen(name, "w");
	setvbuf((*sf)->file, (char*)NULL, _IONBF, 0);
	fprintf((*sf)->file, "conf_id,receiver_id,sender_id,send_time,recv_time,type\n");
}

void statfile_close(struct StatFile** sf) {
	if (*sf != NULL) {
		fclose((*sf)->file);
		free(*sf);
		*sf = NULL;
	}
}

void statfile_mark_arrived(struct StatFile* sf,
                           struct SContext* ctx,
                           part_id_t part_id,
                           enum AVMediaType mtype,
                           struct AVFrame* frame) {
	if (sf != NULL) {
		char buffer[256];
		snprintf(buffer,
		         sizeof(buffer),
		         "%llu,%llu,%llu,%llu,%llu,%s\n",
		         selecon_get_conf_id(ctx),
		         selecon_get_self_id(ctx),
		         part_id,
		         selecon_get_start_ts(ctx) +
		             av_rescale_q(frame->pts, frame->time_base, av_make_q(1, 1000000000)),
		         get_curr_timestamp(),
		         frame->sample_rate == 0 ? "video" : "audio");
		fputs(buffer, sf->file);
	}
}
