#include <gtest/gtest.h>

#include <cstdio>
#include <deque>

extern "C" {
#include <libavutil/frame.h>
}

#include "config.h"
#include "selecon.h"

static std::deque<std::pair<part_id_t, AVFrame*>> user1RcvQueue;
static std::deque<std::pair<part_id_t, AVFrame*>> user2RcvQueue;

void user1_rcv_handler(void *user_data, part_id_t part_id, AVMediaType mtype, AVFrame* frame) {
  frame = av_frame_clone(frame);
	user1RcvQueue.push_back(std::make_pair(part_id, frame));
}

void user2_rcv_handler(void *user_data, part_id_t part_id, AVMediaType mtype, AVFrame* frame) {
  frame = av_frame_clone(frame);
	user2RcvQueue.push_back(std::make_pair(part_id, frame));
}

AVFrame* create_audio_frame(double& time) {
  AVFrame* frame     = av_frame_alloc();
	frame->format      = SELECON_DEFAULT_AUDIO_SAMPLE_FMT;
  frame->sample_rate = SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
	frame->nb_samples  = SELECON_DEFAULT_AUDIO_FRAME_SIZE;
  frame->time_base   = av_make_q(1, SELECON_DEFAULT_AUDIO_SAMPLE_RATE);
  frame->pts         = time * SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
	av_channel_layout_default(&frame->ch_layout, SELECON_DEFAULT_AUDIO_CHANNELS);
	av_frame_get_buffer(frame, 0);
  // planar floating point expected
	for (int i = 0; i < SELECON_DEFAULT_AUDIO_FRAME_SIZE; ++i) {
    for (int c = 0; c < SELECON_DEFAULT_AUDIO_CHANNELS; ++c)
      reinterpret_cast<float*>(frame->data[c])[i] = sinf(time * M_PI * 440);
    time += 1.0 / SELECON_DEFAULT_AUDIO_SAMPLE_RATE;
  }
  return frame;
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
    for (auto& [_, frame] : user1RcvQueue)
      av_frame_free(&frame);
		user1RcvQueue.clear();
    for (auto& [_, frame] : user2RcvQueue)
      av_frame_free(&frame);
		user2RcvQueue.clear();
	}

protected:
	struct SContext* user1Ctx;
	struct SContext* user2Ctx;
	char* user1SockAddr;
	char* user2SockAddr;
};

TEST_F(P2P, inviteReject) {
	auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, NULL, NULL);
	ASSERT_EQ(err, SELECON_OK);
	err = selecon_context_init2(user2Ctx, user2SockAddr, selecon_reject_any, NULL, NULL);
	ASSERT_EQ(err, SELECON_OK);

	sleep(1);

	// send invite from user1 to user2
	err = selecon_invite2(user1Ctx, user2SockAddr);
	ASSERT_EQ(err, SELECON_INVITE_REJECTED);
}

TEST_F(P2P, inviteAccept) {
	auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, NULL, NULL);
	ASSERT_EQ(err, SELECON_OK);
	err = selecon_context_init2(user2Ctx, user2SockAddr, NULL, NULL, NULL);
	ASSERT_EQ(err, SELECON_OK);

	sleep(1);

	// send invite from user1 to user2
	err = selecon_invite2(user1Ctx, user2SockAddr);
	ASSERT_EQ(err, SELECON_OK);
}

TEST_F(P2P, passData) {
	auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL, NULL, user1_rcv_handler);
	ASSERT_EQ(err, SELECON_OK);
	err = selecon_context_init2(user2Ctx, user2SockAddr, NULL, NULL, user2_rcv_handler);

	sleep(1);

	err = selecon_invite2(user1Ctx, user2SockAddr);
	ASSERT_EQ(err, SELECON_OK);

	// send audio data from user1 to user2
	sstream_id_t audio_stream;
	err = selecon_stream_alloc_audio(user1Ctx, &audio_stream);
	ASSERT_EQ(err, SELECON_OK);

  double time = 0.0;
  // push 1 second of audio frames
  while (time < 1.0) {
    AVFrame* frame = create_audio_frame(time);
    err = selecon_stream_push_frame(user1Ctx, audio_stream, &frame);
    av_frame_free(&frame);
    ASSERT_EQ(err, SELECON_OK);
  }

  // wait until data transfered
	sleep(5);

	// expect to rcv some frames in user2 queue
  std::cout << "user2 recvd " << user2RcvQueue.size() << " frames (~"
    << std::setprecision(4)
    << user2RcvQueue.size() * (double)SELECON_DEFAULT_AUDIO_FRAME_SIZE / SELECON_DEFAULT_AUDIO_SAMPLE_RATE
    << " seconds)" << std::endl;
	ASSERT_GT(user2RcvQueue.size(), 0);

	// TODO: send audio data from user2 to user1
}
