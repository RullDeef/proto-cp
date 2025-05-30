#pragma once

#include <pthread.h>

#include "endpoint.h"
#include "participant.h"
#include "stream.h"
#include "stypes.h"

struct SContext {
	bool initialized;

	// conference id. Used to detect that users participate in the same conference. At start
	// initialized with random value. By default, conf id of incoming invite will be examined and if
	// it will differ from this one - this user will send all other participants (if any) that it is
	// leaving current conference before entering new one
	conf_id_t conf_id;

	// timestamp of conference start
	timestamp_t conf_start_ts;

	// array of connections to all other conference participants
	struct SParticipant *participants;

	// number of elements in participants array plus one (self)
	size_t nb_participants;

	// read write lock for access to participants array
	pthread_rwlock_t part_rwlock;

	// connection for self is NULL
	struct SParticipant self;

	// listener thread for incoming invitations
	pthread_t listener_thread;
	struct SEndpoint listen_ep;
	invite_handler_fn_t invite_handler;

	// handler for textual messages. Can be NULL - ignores all messages
	text_handler_fn_t text_handler;

	// recver thread for messages from active conference participants. Valid for nb_participants > 1
	pthread_t conf_thread;
	bool conf_thread_working;

	struct SStreamContainer streams;
};
