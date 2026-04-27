#!/usr/bin/env bash
set -euo pipefail

if ./build/h2d --max-connections -1 >/dev/null 2>&1; then
  echo "expected --max-connections -1 to be rejected" >&2
  exit 1
fi

echo "cli_test: ok"
