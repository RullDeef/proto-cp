#include "role.h"

const char* srole_str(enum SRole role) {
	switch (role) {
		case SROLE_CLIENT: return "client";
		case SROLE_TRANSCODER: return "transcoder";
		case SROLE_TRANSMITTER: return "transmitter";
		default: return "unknown";
	}
}
