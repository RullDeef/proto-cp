#include "error.h"

const char* serror_str(enum SError err) {
  switch (err) {
    case SELECON_OK: return "ok";
    case SELECON_INVALID_ARG: return "invalid argument";
    case SELECON_INVALID_ADDRESS: return "invalid address";
    case SELECON_CON_ERROR: return "connection error";
    case SELECON_CON_TIMEOUT: return "connection timeout";
    case SELECON_ALREADY_INIT: return "already init";
    case SELECON_EMPTY_CONTEXT: return "empty context";
    case SELECON_PTHREAD_ERROR: return "pthread error";
   default: return "unknown error";
  }
}
