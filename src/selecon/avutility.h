#pragma once

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
