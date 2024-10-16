#pragma once

#define SELECON_VERSION_MAJOR 0
#define SELECON_VERSION_MINOR 0
#define SELECON_VERSION_PATCH 1

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define SELECON_VERSION_STRING                                                 \
  STRINGIFY(SELECON_VERSION_MAJOR)                                             \
  "." STRINGIFY(SELECON_VERSION_MINOR) "." STRINGIFY(SELECON_VERSION_PATCH)
