#!/usr/bin/env bash
set -euo pipefail

tmp_dir=$(mktemp -d)
marker=$(mktemp)

cleanup() {
  rm -rf "$tmp_dir" "$marker"
}
trap cleanup EXIT

mkdir -p "$tmp_dir/phase1/http2"
cp ../Makefile.common "$tmp_dir/phase1/Makefile.common"
cp Makefile "$tmp_dir/phase1/http2/Makefile"
cp -R include src "$tmp_dir/phase1/http2/"

mkdir -p "$tmp_dir/phase1/http2/build"
: > "$tmp_dir/phase1/http2/build/stale"

make -C "$tmp_dir/phase1/http2" -j 16 clean all

test -x "$tmp_dir/phase1/http2/build/h2d"
test ! -e "$tmp_dir/phase1/http2/build/stale"
test "$tmp_dir/phase1/http2/build/h2d" -nt "$marker"
