#include "role.h"

const char* srole_str(enum SRole role) {
	switch (role) {
		case SROLE_ORGANISATOR: return "org";
		case SROLE_LISTENER: return "listener";
		default: return "unknown";
	}
}
