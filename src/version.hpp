#ifndef NATIVE_CLIENT_VERSION_H_
#define NATIVE_CLIENT_VERSION_H_

#include <cstdint>

class QString;

namespace version {
extern const uint8_t commit[20];
extern const char tag[];
extern const bool dirty;
extern const uint32_t commits_since_tag;

QString string();
}

#endif
