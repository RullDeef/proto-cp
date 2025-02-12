#pragma once

#include <stddef.h>

#include "endpoint.h"
#include "participant.h"
#include "stypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

enum SMsgType {
	// conference invite. Includes information about conference in general as well as person, who
	// sends this message. Must be accepted or rejected by receiver
	SMSG_INVITE = 0,

	// invite accepted. Contains address of invited participant that is listening for incoming
	// connections
	SMSG_INVITE_ACCEPT = 1,

	// invite rejected. No additional data
	SMSG_INVITE_REJECT = 2,

	// participants list updated. Keeps list of participant with their state change (joined or
	// leaving). Send everybody this message with yourself marked as leaving to gracefully leave
	// the conference
	SMSG_PART_PRESENCE = 3,

	// participant information updated, not including connection state
	SMSG_PART_INFO = 4,

	// participant disconnected accidently and need to reconnect. Upon successfull reconnection
	// other side must send SMSG_REENTER_CONFIRM message back. If verification failed - other side
	// immediately disconnects us
	SMSG_REENTER = 5,

	SMSG_REENTER_CONFIRM = 6,

	SMSG_LEAVE = 7,

	// textual data (aka conference chat)
	SMSG_TEXT = 8,

	// audio packet received. Contains recording timestamp and id of participants besides actual
	// audio data
	SMSG_AUDIO = 9,

	// video packet received. Contains recording timestamp and id of participants with regions in
	// merged frames
	SMSG_VIDEO = 10,
};

// general message interface for passing between participants.
// Can be data frame with encoded audio/video frame or
// control message.
struct SMessage {
	size_t size;  // actual message size including this field
	enum SMsgType type;
	// message-specific params here
};

struct SMsgInvite {
	struct SMessage base;
	conf_id_t conf_id;
	part_id_t part_id;
	timestamp_t conf_start_ts;
	struct SEndpoint listen_ep;
	char part_name[];
};

struct SMsgInviteAccept {
	struct SMessage base;
	part_id_t part_id;    // invited participant id
	struct SEndpoint ep;  // listening endpoint
	char part_name[];     // invited participant name (NULL-terminated)
};

// struct SMsgReject;

struct SMsgPartPresence {
	struct SMessage base;
	part_id_t part_id;
	enum SRole part_role;
	struct SEndpoint ep;
	enum PresenceState {
		PART_JOIN,
		PART_LEAVE,
	} state;
};

struct SMsgPartInfo {
	struct SMessage base;
	part_id_t part_id;
	enum SRole part_role;
	char part_name[];
};

struct SMsgReenter {
	struct SMessage base;
	conf_id_t conf_id;
	part_id_t part_id;
};

struct SMsgReenterConfirm {
	struct SMessage base;
};

// TODO: handle
struct SMsgLeave {
	struct SMessage base;
	part_id_t part_id;
};

struct SMsgText {
	struct SMessage base;
	part_id_t part_id;
	char data[];  // NULL-terminated string
};

struct SMsgAudio {
	struct SMessage base;
	part_id_t part_id;  // some participants can play 'retransmitor' role and transfer
	                    // others' packets
	uint8_t data[];     // size of payload must be calculated dynamically from base.size field
};

struct SMsgVideo {
	struct SMessage base;
	part_id_t part_id;
	uint8_t data[];
};

#pragma pack(pop)

struct SMessage* message_alloc(size_t size);
struct SMessage* message_alloc2(size_t size, enum SMsgType type);
void message_free(struct SMessage** msg);

struct SMessage* message_invite_alloc(conf_id_t conf_id,
                                      timestamp_t conf_start_ts,
                                      part_id_t part_id,
                                      const struct SEndpoint* listen_ep,
                                      const char* part_name);
struct SMessage* message_invite_accept_alloc(part_id_t id, const char* name, struct SEndpoint* ep);
struct SMessage* message_invite_reject_alloc(void);

struct SMsgPartPresence* message_part_presence_alloc(void);

struct SMsgReenter* message_reenter_alloc(conf_id_t conf_id, part_id_t part_id);

struct SMessage* message_reenter_confirm_alloc(void);

// allocate text message
struct SMessage* message_text_alloc(part_id_t source, const char* text);

// allocate audio/video packet message and fill it with data from packet
struct SMessage* message_audio_alloc(part_id_t source, struct AVPacket* packet);
struct SMessage* message_video_alloc(part_id_t source, struct AVPacket* packet);

#ifdef __cplusplus
}  // extern "C"
#endif
