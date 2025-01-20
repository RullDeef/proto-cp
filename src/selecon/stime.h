#pragma once

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "stypes.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline timestamp_t get_curr_timestamp(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline void reduce_fps(timestamp_t expected_delta, timestamp_t *prev_ts) {
	if (prev_ts == NULL)
		return;
	else if (*prev_ts == 0)
		*prev_ts = get_curr_timestamp();
	else {
		timestamp_t now = get_curr_timestamp();
		if (now > *prev_ts) {
			timestamp_t delta = now - *prev_ts;
			if (expected_delta > delta)
				usleep((expected_delta - delta) / 1000);
		}
		*prev_ts = now;
	}
}

#ifdef __cplusplus
}
#endif
