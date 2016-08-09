#!/usr/bin/env sh
set -eu

usage() {
    echo "$0 <source directory>"
    exit 1
}

[ "$#" -eq 1 ] || usage

SOURCE_DIR=$1

_git() {
  git -C "$SOURCE_DIR" "$@"
}

COMMIT=$(_git rev-parse HEAD)
COMMIT_STR="0x$(echo $COMMIT |cut -b 1-2)"
for i in `seq 3 2 39`; do
    COMMIT_STR="$COMMIT_STR, 0x$(echo $COMMIT |cut -b $i-$((i+1)))"
done
TAG=$(_git describe --abbrev=0 --tags --always)
cat <<EOF
#include "version.hpp"
namespace version {
const uint8_t commit[20] = {$COMMIT_STR};
const char tag[] = "$TAG";
const bool dirty = $(if _git diff-index --quiet HEAD; then echo "false"; else echo "true"; fi);
const uint32_t commits_since_tag = $(_git rev-list --count $TAG..$COMMIT);
}
EOF
