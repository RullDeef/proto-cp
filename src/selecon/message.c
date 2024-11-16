#include "message.h"
#include <stdlib.h>

struct SMessage* message_alloc(size_t size) {
    struct SMessage* msg = malloc(size);
    msg->size = size;
    return msg;
}

void message_free(struct SMessage** msg) {
    if (msg != NULL && *msg != NULL) {
        free(*msg);
        *msg = NULL;
    }
}
