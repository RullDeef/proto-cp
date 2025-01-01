#include <gtest/gtest.h>

#include <cstdio>
#include <deque>

extern "C" {
#include <libavutil/frame.h>
}

#include "config.h"
#include "selecon.h"

static std::map<SContext*, std::deque<std::pair<part_id_t, std::string>>> userTextQueue;
static std::map<SContext*, std::deque<std::pair<part_id_t, AVFrame*>>> userRcvQueue;

static void media_handler(void* user_data, part_id_t part_id, AVMediaType mtype, AVFrame* frame) {
	frame = av_frame_clone(frame);
	userRcvQueue[reinterpret_cast<SContext*>(user_data)].push_back(std::make_pair(part_id, frame));
}

static void text_handler(void* user_data, part_id_t part_id, const char* text) {
	userTextQueue[reinterpret_cast<SContext*>(user_data)].push_back(
	    std::make_pair(part_id, std::string(text)));
}

static AVFrame* create_audio_frame(double& time) {
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

// N - amount of allocated contexts for participants
template <size_t N>
class Multi : public testing::Test {
public:
	virtual void SetUp() override {
		srand(time(NULL));
		for (size_t i = 0; i < N; ++i) {
			sockAddrs[i] = (char*)malloc(1024);
			snprintf(sockAddrs[i], 1024, "file:///tmp/%d.sock", rand());
			ctxs[i] = selecon_context_alloc();
			SError err =
			    selecon_context_init2(ctxs[i], sockAddrs[i], NULL, text_handler, media_handler);
			if (err != SELECON_OK)
				throw testing::AssertionFailure()
				    << "failed to init selecon context: " << serror_str(err);
		}
		sleep(1);
	}

	virtual void TearDown() override {
		for (size_t i = 0; i < N; ++i) {
			selecon_context_free(&ctxs[i]);
			remove(sockAddrs[i] + sizeof("file://") - 1);
			free(sockAddrs[i]);
			for (auto& [_, frame] : userRcvQueue[ctxs[i]]) av_frame_free(&frame);
			userRcvQueue[ctxs[i]].clear();
		}
	}

	const size_t participants_count = N;

protected:
	std::array<struct SContext*, N> ctxs;
	std::array<char*, N> sockAddrs;
};

class Multi4 : public Multi<4> {};

TEST_F(Multi4, invite3) {
	// send invite from user1 to user2
	SError err = selecon_invite2(ctxs[0], sockAddrs[1]);
	EXPECT_EQ(err, SELECON_OK);

	// send invite from user1 to user3
	err = selecon_invite2(ctxs[0], sockAddrs[2]);
	EXPECT_EQ(err, SELECON_OK);

	// send invite from user1 to user4
	err = selecon_invite2(ctxs[0], sockAddrs[3]);
	EXPECT_EQ(err, SELECON_OK);
}

TEST_F(Multi4, passAudioData) {
	SError err = selecon_invite2(ctxs[0], sockAddrs[1]);
	EXPECT_EQ(err, SELECON_OK);
	err = selecon_invite2(ctxs[0], sockAddrs[2]);
	EXPECT_EQ(err, SELECON_OK);
	err = selecon_invite2(ctxs[0], sockAddrs[3]);
	EXPECT_EQ(err, SELECON_OK);

	// send audio data from user1 to user2
	sstream_id_t audio_stream = NULL;
	err                       = selecon_stream_alloc_audio(ctxs[0], &audio_stream);
	ASSERT_EQ(err, SELECON_OK);

	double time = 0.0;
	// push 1 second of audio frames
	while (time < 1.0) {
		AVFrame* frame = create_audio_frame(time);
		err            = selecon_stream_push_frame(ctxs[0], audio_stream, &frame);
		av_frame_free(&frame);
		ASSERT_EQ(err, SELECON_OK);
	}

	// wait until data transfered
	sleep(5);

	// expect to rcv some frames in others' queues
	for (size_t i = 1; i < participants_count; ++i) {
		const auto frames = userRcvQueue[ctxs[i]].size();
		std::cout << "user" << i + 1 << " recvd " << frames << " frames (~" << std::setprecision(4)
		          << frames * (double)SELECON_DEFAULT_AUDIO_FRAME_SIZE /
		                 SELECON_DEFAULT_AUDIO_SAMPLE_RATE
		          << " seconds)" << std::endl;
		EXPECT_GT(userRcvQueue[ctxs[i]].size(), 0);
	}
}
