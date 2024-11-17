#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum SRole {
	SROLE_CLIENT,
	SROLE_TRANSMITTER,
	SROLE_TRANSCODER,
};

const char* srole_str(enum SRole role);

#ifdef __cplusplus
}  // extern "C"
#endif
