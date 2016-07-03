#ifndef NATIVE_CHAT_MATRIX_PARSE_HPP_
#define NATIVE_CHAT_MATRIX_PARSE_HPP_

#include "proto.hpp"

namespace matrix {

proto::Sync parse_sync(QJsonValue v);

}

#endif
