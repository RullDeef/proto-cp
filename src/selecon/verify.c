#include "verify.h"

#include <openssl/sha.h>

conf_id_t generate_conf_id(part_id_t org_id, timestamp_t conf_start) {
	unsigned char digest[SHA512_DIGEST_LENGTH];
	// SHA512_CTX ctx;
	// SHA512_Init(&ctx);
	// SHA512_Update(&ctx, &org_id, sizeof(part_id_t));
	// SHA512_Update(&ctx, &conf_start, sizeof(timestamp_t));
	// SHA512_Final(digest, &ctx);
#pragma pack(push, 0)
	struct {
		part_id_t org_id;
		timestamp_t conf_start;
	} data = {.org_id = org_id, .conf_start = conf_start};
#pragma pack(pop)
	SHA512((const unsigned char*)&data, sizeof(data), digest);
	return *(conf_id_t*)digest;
}

bool verify_conf_id(conf_id_t conf_id, part_id_t org_id, timestamp_t conf_start) {
	return conf_id == generate_conf_id(org_id, conf_start);
}
