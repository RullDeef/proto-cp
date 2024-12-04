#include <gtest/gtest.h>

#include <cstdio>
#include <deque>

extern "C" {
#include <libavutil/frame.h>
}

#include "selecon.h"

static std::deque<std::pair<part_id_t, AVFrame*>> user1RcvQueue;
static std::deque<std::pair<part_id_t, AVFrame*>> user2RcvQueue;

void user1_rcv_handler(part_id_t part_id, AVFrame* frame) {
  user1RcvQueue.push_back(std::make_pair(part_id, frame));
}

void user2_rcv_handler(part_id_t part_id, AVFrame* frame) {
  user2RcvQueue.push_back(std::make_pair(part_id, frame));
}

class P2P : public testing::Test {
public:
	virtual void SetUp() override {
		// create temporary named sockets
		user1SockAddr = (char*)malloc(1024);
		user2SockAddr = (char*)malloc(1024);
		srand(time(NULL));
		snprintf(user1SockAddr, 1024, "file:///tmp/%d.sock", rand());
		snprintf(user2SockAddr, 1024, "file:///tmp/%d.sock", rand());
		user1Ctx = selecon_context_alloc();
		user2Ctx = selecon_context_alloc();
	}

	virtual void TearDown() override {
		// remove temp sockets
		selecon_context_free(&user1Ctx);
		selecon_context_free(&user2Ctx);
		remove(user1SockAddr + sizeof("file://") - 1);
		remove(user2SockAddr + sizeof("file://") - 1);
		free(user1SockAddr);
		free(user2SockAddr);
    user1RcvQueue.clear();
    user2RcvQueue.clear();
	}

protected:
	struct SContext* user1Ctx;
	struct SContext* user2Ctx;
  char* user1SockAddr;
  char* user2SockAddr;
};

TEST_F(P2P, inviteReject) {
	auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, NULL);
  ASSERT_EQ(err, SELECON_OK);
  err = selecon_context_init2(user2Ctx, user2SockAddr, selecon_reject_any, NULL);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);

  // send invite from user1 to user2
  err = selecon_invite2(user1Ctx, user2SockAddr);
  ASSERT_EQ(err, SELECON_INVITE_REJECTED);
}

TEST_F(P2P, inviteAccept) {
  auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, NULL);
  ASSERT_EQ(err, SELECON_OK);
  err = selecon_context_init2(user2Ctx, user2SockAddr, NULL, NULL);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);

  // send invite from user1 to user2
  err = selecon_invite2(user1Ctx, user2SockAddr);
  ASSERT_EQ(err, SELECON_OK);
}

TEST_F(P2P, passData) {
  auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, user1_rcv_handler);
  ASSERT_EQ(err, SELECON_OK);
  err = selecon_context_init2(user2Ctx, user2SockAddr, NULL, user2_rcv_handler);

  sleep(1);

  err = selecon_invite2(user1Ctx, user2SockAddr);
  ASSERT_EQ(err, SELECON_OK);

  // send audio data from user1 to user2
  sstream_id_t audio_stream;
  err = selecon_stream_alloc_audio(user1Ctx, &audio_stream);
  ASSERT_EQ(err, SELECON_OK);

  AVFrame *frame = av_frame_alloc();
  frame->format = AV_SAMPLE_FMT_S16;
  frame->nb_samples = 960;
  av_channel_layout_default(&frame->ch_layout, 1);
  av_frame_get_buffer(frame, 0);
  for (int i = 0; i < 960; ++i)
    frame->data[0][i] = static_cast<uint8_t>(i % 64);

  err = selecon_stream_push_audio(user1Ctx, audio_stream, frame);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);

  // expect to rcv frame in user2 queue
  ASSERT_EQ(user2RcvQueue.size(), 1);

  // send audio data from user2 to user1
}