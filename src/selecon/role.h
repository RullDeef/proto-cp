#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum SRole {
	SROLE_ORGANISATOR = 0,
	SROLE_LISTENER    = 1,
};

const char* srole_str(enum SRole role);

#ifdef __cplusplus
}  // extern "C"
#endif
