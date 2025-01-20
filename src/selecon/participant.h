#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "endpoint.h"
#include "role.h"
#include "stime.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SConnection;

typedef unsigned long long part_id_t;

struct SParticipant {
	// id used for uniquely identify participants in single conference.
	// Id alsa determines order in broadcasting messages.
	part_id_t id;
	enum SRole role;
	// display name can be not unique in same conference.
	// Allocated dynamically
	char* name;
	// listening endpoint for reconnection
	struct SEndpoint listen_ep;
	// direct connection from this participant
	struct SConnection* connection;

	// reconnection state. When connection is NULL and this is not self - hangup timestamp is valid
	timestamp_t hangup_timestamp;
};

struct SParticipant spart_init(const char* name);

// frees allocated resources
void spart_destroy(struct SParticipant* par);

// participant disconnected accidently. Sets up hangup timestamp for future reconnection detection
void spart_hangup(struct SParticipant* par);

// returns true, if connection restoration since hangup is still possible (timeout not expired).
// Also updates connection and resets hangup timestamp.
//
// If connection is alive, returns false to forbid connection hijacking
bool spart_hangup_validate(struct SParticipant* par, struct SConnection* new_con);

// returns true when participant hanged up and wait timeout is over
bool spart_hangup_timedout(struct SParticipant* par);

void spart_rename(struct SParticipant* part, const char* new_name);

void spart_dump(FILE* fd, struct SParticipant* par);

#ifdef __cplusplus
}  // extern "C"
#endif
