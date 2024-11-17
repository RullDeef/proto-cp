#include "participant.h"

#include <stddef.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

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
