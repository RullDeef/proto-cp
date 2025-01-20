#include "participant.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "connection.h"
#include "stime.h"

static unsigned long long generate_id() {
	return rand() % ULONG_MAX;
}

struct SParticipant spart_init(const char* name) {
	struct SParticipant par;
	par.id               = generate_id();
	par.name             = strdup(name);
	par.role             = SROLE_CLIENT;
	par.connection       = NULL;
	par.hangup_timestamp = 0;
	return par;
}

void spart_destroy(struct SParticipant* par) {
	if (par) {
		free(par->name);
		par->name = NULL;
		sconn_disconnect(&par->connection);
	}
}

void spart_hangup(struct SParticipant* par) {
	if (par->connection) {
		sconn_disconnect(&par->connection);
		par->hangup_timestamp = get_curr_timestamp();
	}
}

bool spart_hangup_validate(struct SParticipant* par, struct SConnection* con) {
	if (par->hangup_timestamp == 0)
		return false;
	timestamp_t now      = get_curr_timestamp();
	timestamp_t delta_ms = (now - par->hangup_timestamp) / 1000000;
	if (delta_ms >= SELECON_DEFAULT_REENTER_TIMEOUT)
		return false;
	par->connection       = con;
	par->hangup_timestamp = 0;
	return true;
}

bool spart_hangup_timedout(struct SParticipant* par) {
	if (par->hangup_timestamp == 0)
		return false;
	timestamp_t now      = get_curr_timestamp();
	timestamp_t delta_ms = (now - par->hangup_timestamp) / 1000000;
	return delta_ms >= SELECON_DEFAULT_REENTER_TIMEOUT;
}

void spart_rename(struct SParticipant* part, const char* new_name) {
	free(part->name);
	part->name = strdup(new_name);
}

void spart_dump(FILE* fd, struct SParticipant* par) {
	if (par == NULL)
		fprintf(fd, "(null)");
	else
		fprintf(fd, "%10llu %s [%s]", par->id, par->name, srole_str(par->role));
}
