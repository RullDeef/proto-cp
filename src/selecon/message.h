#pragma once

#include <stddef.h>

struct SFrameMessage;

#pragma pack(push, 1)

enum SMsgType {
    SMSG_HELLO,
    SMSG_BYE,
};

// general message interface for passing between participants.
// Can be data frame with encoded audio/video frame or
// control message.
struct SMessage {
    size_t size; // actual message size including this field
    enum SMsgType type;
    // message-specific params here
};

#pragma pack(pop)

struct SMessage* message_alloc(size_t size);
void message_free(struct SMessage** msg);
