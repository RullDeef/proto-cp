#pragma once

#include "stypes.h"

conf_id_t generate_conf_id(part_id_t org_id, timestamp_t conf_start);
bool verify_conf_id(conf_id_t conf_id, part_id_t org_id, timestamp_t conf_start);
