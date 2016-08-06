#!/usr/bin/env sh
set -eu

COMMIT=$(git rev-parse HEAD)
COMMIT_STR="0x${COMMIT:0:2}"
for i in `seq 2 2 38`; do
    COMMIT_STR="$COMMIT_STR, 0x${COMMIT:$i:2}"
done
TAG=$(git describe --abbrev=0 --tags --always)
cat <<EOF
#include "version.hpp"
namespace version {
const uint8_t commit[20] = {$COMMIT_STR};
const char tag[] = "$TAG";
const bool dirty = $(if git diff-index --quiet HEAD; then echo "false"; else echo "true"; fi);
const uint32_t commits_since_tag = $(git rev-list --count $TAG..$COMMIT);
}
EOF
