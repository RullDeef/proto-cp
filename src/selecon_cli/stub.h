#pragma once

// helper functions for stubbing audio/video in conference

struct Stub;
struct SContext;

struct Stub* stub_create_dynamic(struct SContext* context, const char* media_file);

void stub_close(struct Stub** stub);
