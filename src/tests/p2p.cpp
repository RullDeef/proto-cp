#include <gtest/gtest.h>

#include <cstdio>

#include "selecon.h"

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
	}

protected:
	struct SContext* user1Ctx;
	struct SContext* user2Ctx;
  char* user1SockAddr;
  char* user2SockAddr;
};

TEST_F(P2P, inviteReject) {
	auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL);
  ASSERT_EQ(err, SELECON_OK);
  err = selecon_context_init2(user2Ctx, user2SockAddr, selecon_reject_any);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);

  // send invite from user1 to user2
  err = selecon_invite2(user1Ctx, user2SockAddr);
  ASSERT_EQ(err, SELECON_INVITE_REJECTED);
}

TEST_F(P2P, inviteAccept) {
  auto err = selecon_context_init2(user1Ctx, user1SockAddr, NULL);
  ASSERT_EQ(err, SELECON_OK);
  err = selecon_context_init2(user2Ctx, user2SockAddr, NULL);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);

  // send invite from user1 to user2
  err = selecon_invite2(user1Ctx, user2SockAddr);
  ASSERT_EQ(err, SELECON_OK);

  sleep(1);
}
