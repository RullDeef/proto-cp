#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum SError {
	SELECON_OK = 0,
	SELECON_INVALID_ARG,
	SELECON_INVALID_ADDRESS,
	SELECON_CON_ERROR,
	SELECON_CON_TIMEOUT,
	SELECON_CON_HANGUP,
	SELECON_ALREADY_INIT,
	SELECON_EMPTY_CONTEXT,
	SELECON_PTHREAD_ERROR,
	SELECON_UNEXPECTED_MSG_TYPE,
	SELECON_INVITE_REJECTED,
};

const char* serror_str(enum SError err);

#ifdef __cplusplus
}  // extern "C"
#endif
