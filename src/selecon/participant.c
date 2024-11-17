#include "participant.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "connection.h"

static unsigned long long generate_id() {
	srand(time(NULL));
	return rand() % ULONG_MAX;
}

struct SParticipant spart_init(const char* name) {
	struct SParticipant par;
	par.id         = generate_id();
	par.name       = strdup(name);
	par.role       = SROLE_CLIENT;
	par.connection = NULL;
	return par;
}

void spart_destroy(struct SParticipant* par) {
	if (par) {
		free(par->name);
		par->name = NULL;
		sconn_disconnect(&par->connection);
	}
}

void spart_dump(FILE* fd, struct SParticipant* par) {
	if (par == NULL)
		fprintf(fd, "(null)");
	else
		fprintf(fd, "%10llu %s [%s]", par->id, par->name, srole_str(par->role));
}
