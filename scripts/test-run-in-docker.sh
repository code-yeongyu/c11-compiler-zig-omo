#!/usr/bin/env bash
# scripts/test-run-in-docker.sh
#
# Smoke tests for scripts/run-in-docker.sh. The real docker binary is replaced
# on PATH with a stub that just records its argv; we then assert the recorded
# argv matches what we expect run-in-docker.sh to launch. No actual Docker
# daemon is needed.
#
# Regression coverage:
#   - `-- IMAGE=...` separator must NOT swallow the next argument
#     (cubic-dev-ai PR #8 finding scripts/run-in-docker.sh:38).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat >"$WORK/docker" <<'STUB'
#!/usr/bin/env bash
printf '%s\0' "$@" >"$DOCKER_STUB_ARGV"
STUB
chmod +x "$WORK/docker"

run_under_stub() {
  DOCKER_STUB_ARGV="$WORK/argv.bin" PATH="$WORK:$PATH" \
    "$ROOT/scripts/run-in-docker.sh" "$@" >/dev/null
}

argv_has() {
  local needle="$1"
  local file="$WORK/argv.bin"
  python3 - "$file" "$needle" <<'PY'
import sys
path, needle = sys.argv[1], sys.argv[2]
data = open(path, 'rb').read()
parts = [p.decode() for p in data.split(b'\x00') if p != b'']
sys.exit(0 if needle in parts else 1)
PY
}

argv_dump() {
  python3 - "$WORK/argv.bin" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
parts = [p.decode() for p in data.split(b'\x00') if p != b'']
for i, part in enumerate(parts):
    print(f'{i}: {part!r}')
PY
}

fail() {
  echo "FAIL: $1" >&2
  echo "captured docker argv:" >&2
  argv_dump >&2
  exit 1
}

test_dash_dash_separator_preserves_image() {
  run_under_stub c11-ref all -- IMAGE=gcc:13
  argv_has 'gcc:13' || fail 'IMAGE=gcc:13 was dropped after the -- separator'
  echo 'ok: test_dash_dash_separator_preserves_image'
}

test_dash_dash_separator_preserves_image

echo 'all run-in-docker tests passed'
