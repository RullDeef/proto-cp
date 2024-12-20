#pragma once

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>

#define TRACE_CALL \
  do { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, __func__); \
  } while(0)

#define TRACE(fmt, ...) \
  do { \
    fprintf(stderr, "%s:%d: " fmt "\n", __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__); \
  } while (0)

#define TRACE_THRD(fmt, ...) \
  do { \
    fprintf(stderr, "%s:%d: tid=%d " fmt "\n", __FILE__, __LINE__, gettid() __VA_OPT__(,) __VA_ARGS__); \
  } while (0)
