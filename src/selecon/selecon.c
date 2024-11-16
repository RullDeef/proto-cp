#include "selecon.h"
#include "config.h"
#include "connection.h"
#include "endpoint.h"
#include "error.h"
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include "message.h"
#include <stdio.h>

#include <pthread.h>
#include <string.h>

struct SContext {
  bool initialized;

  // array of connections to all conference participants.
  // For current participant connection value is NULL
  struct SConnection **connections;

  // number of connections in array
  unsigned int nb_participants;

  // 0-based index of this participant
  unsigned int part_id;

  char **part_names;
  enum SRole *part_roles;

  // listener thread for incoming invitations
  pthread_t listener_thread;
  struct SEndpoint listen_ep;
};

struct SContext *selecon_context_alloc(void) {
  struct SContext *context = calloc(1, sizeof(struct SContext));
  return context;
}

void scontext_destroy(struct SContext *context) {
  if (context->initialized) {
    context->initialized = false;
    pthread_join(context->listener_thread, NULL);
    for (int i = 0; i < context->nb_participants; ++i) {
      free(context->part_names[i]);
      sconn_disconnect(&context->connections[i]);
    }
    free(context->part_names);
    free(context->connections);
  }
}

void selecon_context_free(struct SContext **context) {
  if (*context) {
    scontext_destroy(*context);
    free(*context);
    *context = NULL;
  }
}

static void *listener_func(void *arg) {
  struct SContext *ctx = arg;
  struct SConnection *listen_con;
  struct SConnection *part_con;
  
  enum SError err = sconn_listen(&listen_con, &ctx->listen_ep);
  if (err != SELECON_OK) {
    perror("sconn_listen");
    return NULL;
  }

  while (ctx->initialized) {
    err = sconn_accept(listen_con, &part_con, 5000);
    if (err == SELECON_CON_TIMEOUT)
      continue;
    if (err != SELECON_OK) {
      perror("sconn_accept");
      break;
    }
    // interact with participant
    //
    // recv hello message
    // respond with another hello message
    struct SMessage* msg = NULL;
    err = sconn_recv(part_con, &msg);
    if (err != SELECON_OK) {
      perror("sconn_recv");
      sconn_disconnect(&part_con);
      continue;
    }
    // check request message
    printf("recvd message: size = %lu, type = %d\n", msg->size, msg->type);
    message_free(&msg);
    // send response
    msg = message_alloc(sizeof(struct SMessage));
    msg->type = SMSG_HELLO;
    err = sconn_send(part_con, msg);
    if (err != SELECON_OK) {
      perror("sconn_send");
    }
    message_free(&msg);
    // save connected participant to context
    unsigned int part_id = ctx->nb_participants++;
    // TODO: better reallocs
    ctx->connections = realloc(ctx->connections, ctx->nb_participants * sizeof(struct SConnection*));
    ctx->part_names = realloc(ctx->part_names, ctx->nb_participants * sizeof(char*));
    ctx->part_names[part_id] = strdup("new part");
    ctx->connections[part_id] = part_con;
    // part_con = NULL;
    printf("new participant added!\n");
  }

  return NULL;
}

enum SError selecon_context_init(struct SContext *ctx,
                                 struct SEndpoint *ep) {
  if (ctx == NULL)
    return SELECON_INVALID_ARG;
  if (ctx->initialized)
    return SELECON_ALREADY_INIT;
  struct SEndpoint default_ep;
  if (ep == NULL) {
    selecon_parse_endpoint(&default_ep, "127.0.0.1", SELECON_DEFAULT_LISTEN_PORT);
    ep = &default_ep;
  }

  ctx->part_id = 0;
  ctx->part_names = malloc(sizeof(char*));
  ctx->part_names[0] = strdup(SELECON_DEFAULT_PART_NAME);
  ctx->connections = malloc(sizeof(struct SConnection*));
  ctx->nb_participants = 1;

  ctx->initialized = true;
  ctx->listen_ep = *ep;
  int perr = pthread_create(&ctx->listener_thread, NULL, listener_func, ctx);
  if (perr != 0)
    return SELECON_PTHREAD_ERROR;

  enum SError err = SELECON_OK;
  if (err != SELECON_OK)
    scontext_destroy(ctx);
  return err;
}

enum SError selecon_context_init2(struct SContext *context,
                                  const char* address) {
  if (address == NULL) {
    return selecon_context_init(context, NULL);
  } else {
    struct SEndpoint ep;
    enum SError err = selecon_parse_endpoint2(&ep, address);
    if (err != SELECON_OK)
      return err;
    return selecon_context_init(context, &ep);
  }
}

void selecon_context_dump(FILE* fd, struct SContext* context) {
  if (context == NULL) {
    fprintf(fd, "(null)");
    return;
  }
  fprintf(fd, "listen_ep: ");
  selecon_endpoint_dump(fd, &context->listen_ep);
  fprintf(fd, "\nnb_participants: %d\n", context->nb_participants);
  fprintf(fd, "part_id: %d\n", context->part_id);
  for (int i = 0; i < context->nb_participants; ++i)
    fprintf(fd, "  - %s\n", context->part_names[i]);
}

enum SError selecon_invite(struct SContext *context, struct SEndpoint *ep,
                           struct SInvite *invite) {
  if (context == NULL || ep == NULL)
    return SELECON_INVALID_ARG;
  if (!context->initialized)
    return SELECON_EMPTY_CONTEXT;
  struct SInvite default_invite;
  if (invite == NULL) {
    sinvite_fill_defaults(&default_invite);
    invite = &default_invite;
  }
  struct SConnection *con;
  enum SError err = sconn_connect(&con, ep);
  if (err != SELECON_OK)
    return err;
  // send hello message and recv it back
  struct SMessage* msg = message_alloc(sizeof(struct SMessage));
  msg->type = SMSG_HELLO;
  err = sconn_send(con, msg);
  if (err != SELECON_OK) {
    printf("failed to send hello message!\n");
    sconn_disconnect(&con);
    message_free(&msg);
    return err;
  }
  printf("sended hello message\n");
  message_free(&msg);
  err = sconn_recv(con, &msg);
  if (err != SELECON_OK) {
    printf("failed to recv hello message!\n");
    sconn_disconnect(&con);
    return err;
  }
  printf("recvd hello message!\n");
  message_free(&msg);
  // add participant to context
  unsigned int part_id = context->nb_participants++;
  // TODO: better reallocs
  context->connections = realloc(context->connections, context->nb_participants * sizeof(struct SConnection*));
  context->part_names = realloc(context->part_names, context->nb_participants * sizeof(char*));
  context->connections[part_id] = con;
  context->part_names[part_id] = strdup("new part");
  printf("participant invited!\n");
  return err;
}

enum SError selecon_invite2(struct SContext *context, const char* address,
                            struct SInvite *invite_params) {
  struct SEndpoint ep;
  enum SError err = selecon_parse_endpoint2(&ep, address);
  if (err != SELECON_OK)
    return err;
  return selecon_invite(context, &ep, invite_params);
}

enum SError selecon_accept_invite(struct SContext *context, const char *address) {
  if (context == NULL)
    return SELECON_INVALID_ARG;
  if (!context->initialized)
    return SELECON_EMPTY_CONTEXT;

  return SELECON_OK;
  // enum SError err;
  // struct SEndpoint ep;
  // struct SConnection *con;

  // err = selecon_parse_endpoint2(&ep, address);
  // if (err != SELECON_OK)
  //   return err;

  // err = sconn_connect(&con, &ep);
  // if (err != SELECON_OK)
  //   return err;

  // return err;
}

// enters conference and starts sending and receiving media content
enum SError selecon_enter_conference(struct SContext *context);

// stops content sharing
enum SError selecon_leave_conference(struct SContext *context);

// sends audio frame to other participants
enum SError selecon_send_audio_frame(struct SContext *context,
                                     struct SFrame *frame);

// sends video frame to other participants
enum SError selecon_send_video_frame(struct SContext *context,
                                     struct SFrame *frame);
enum SError selecon_enter_conference(struct SContext *context) {
  return SELECON_OK;
}

enum SError selecon_leave_conference(struct SContext *context) {
  return SELECON_OK;
}
