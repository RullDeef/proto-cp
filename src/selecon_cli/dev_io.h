#pragma once

#include <libavutil/frame.h>

#include "error.h"

struct Dev;
struct SContext;

// allocates output streams for each non-NULL dev_name and starts sending frames to streams
struct Dev* dev_create_src(struct SContext* context, const char* adev_name, const char* vdev_name);

// prepares output devices for each non-NULL dev_name
struct Dev* dev_create_sink(const char* adev_name, const char* vdev_name);

// push received frames to output devices
enum SError dev_push_frame(struct Dev* dev, enum AVMediaType mtype, struct AVFrame* frame);

void dev_close(struct Dev** dev);
