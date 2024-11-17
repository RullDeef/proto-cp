#pragma once

#include "connection.h"
#include "role.h"

struct SParticipant {
	// id used for uniquely identify participants in single conference.
	// Id alsa determines order in broadcasting messages.
	unsigned long long id;
	enum SRole role;
	// display name can be not unique in same conference.
  // Allocated dynamically
	char *name;
	// direct connection from this participant
	struct SConnection *connection;
};

struct SParticipant spart_init(const char* name);

// frees allocated resources
void spart_destroy(struct SParticipant* par);
