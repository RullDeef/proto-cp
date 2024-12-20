#include "message.h"

#include <stdlib.h>
#include "avutility.h"

struct SMessage* message_alloc(size_t size) {
	struct SMessage* msg = calloc(1, size);
	msg->size            = size;
	return msg;
}

struct SMessage* message_alloc2(size_t size, enum SMsgType type) {
	struct SMessage* msg = calloc(1, size);
	msg->size            = size;
	msg->type            = type;
	return msg;
}

void message_free(struct SMessage** msg) {
	if (msg != NULL && *msg != NULL) {
		free(*msg);
		*msg = NULL;
	}
}

struct SMessage* message_invite_alloc(conf_id_t conf_id, part_id_t part_id, const char* part_name) {
	size_t size            = sizeof(struct SMsgInvite) + strlen(part_name) + 1;
	struct SMsgInvite* msg = (struct SMsgInvite*)message_alloc2(size, SMSG_INVITE);
	msg->conf_id           = conf_id;
	msg->part_id           = part_id;
	strcpy(msg->part_name, part_name);
	return (struct SMessage*)msg;
}

struct SMessage* message_invite_accept_alloc(part_id_t id, const char* name, struct SEndpoint* ep) {
	size_t size = sizeof(struct SMsgInviteAccept) + strlen(name) + 1;
	struct SMsgInviteAccept* msg =
	    (struct SMsgInviteAccept*)message_alloc2(size, SMSG_INVITE_ACCEPT);
	msg->id = id;
  memcpy(&msg->ep, ep, sizeof(struct SEndpoint));
	msg->ep = *ep;
	strcpy(msg->name, name);
	return (struct SMessage*)msg;
}

struct SMessage* message_invite_reject_alloc(void) {
	return message_alloc2(sizeof(struct SMessage), SMSG_INVITE_REJECT);
}

struct SMsgPartPresence* message_part_presence_alloc(size_t count) {
	size_t size = sizeof(struct SMsgPartPresence) + count * sizeof(struct PartPresenceState);
	struct SMsgPartPresence* msg = (struct SMsgPartPresence*)message_alloc2(size, SMSG_PART_PRESENCE);
	msg->count                   = count;
	return msg;
}

struct SMessage* message_audio_alloc(part_id_t source, struct AVPacket *packet) {
  size_t size = sizeof(struct SMsgAudio) + av_packet_serialize(NULL, packet);
  struct SMsgAudio* msg = (struct SMsgAudio*)message_alloc2(size, SMSG_AUDIO);
  msg->source_part_id = source;
  av_packet_serialize(msg->data, packet);
  return (struct SMessage*)msg;
}

struct SMessage* message_video_alloc(part_id_t source, struct AVPacket *packet) {
  size_t size = sizeof(struct SMsgVideo) + av_packet_serialize(NULL, packet);
  struct SMsgVideo* msg = (struct SMsgVideo*)message_alloc2(size, SMSG_VIDEO);
  msg->source_part_id = source;
  av_packet_serialize(msg->data, packet);
  return (struct SMessage*)msg;
}
