#pragma once

#include <stdio.h>

#include "role.h"

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
	// direct connection from this participant
	struct SConnection* connection;
};

struct SParticipant spart_init(const char* name);

// frees allocated resources
void spart_destroy(struct SParticipant* par);

void spart_rename(struct SParticipant* part, const char* new_name);

void spart_dump(FILE* fd, struct SParticipant* par);

#ifdef __cplusplus
}  // extern "C"
#endif
