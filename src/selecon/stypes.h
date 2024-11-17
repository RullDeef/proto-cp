#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long conf_id_t;
typedef unsigned long long part_id_t;

// participant endpoint abstraction (IP + port)
struct SEndpoint;

// invitation options
struct SInvite;

// data frame for passing between participants
struct SFrame;

// Main struct for holding conference state
struct SContext;

struct SMsgInvite;

// invite handing callback. Returns bool weither or not
// invite was accepted.
typedef bool (*invite_handler_fn_t)(struct SMsgInvite *invite);

#ifdef __cplusplus
}  // extern "C"
#endif
