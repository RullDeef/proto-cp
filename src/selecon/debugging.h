#pragma once

#define TRACE_CALL \
  do { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, __func__); \
  } while(0)
