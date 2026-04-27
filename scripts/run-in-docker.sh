#!/usr/bin/env bash
# scripts/run-in-docker.sh
#
# Reproduce gcc-in-docker CI matrices locally. Same image and volume layout
# as GitHub Actions, with phase-aware build commands.
#
# Usage:
#   scripts/run-in-docker.sh [phase1] <subproject> [target...] [-- IMAGE=gcc:14] [IO=epoll|io_uring]
#   scripts/run-in-docker.sh phase2 <subproject> [target...] [-- IMAGE=gcc:14]
#
# Examples:
#   scripts/run-in-docker.sh c11-ref            # default: print-platform + all + test
#   scripts/run-in-docker.sh http2 all test smoke IO=io_uring
#   scripts/run-in-docker.sh doom all smoke -- IMAGE=gcc:13
#   scripts/run-in-docker.sh phase2 zcc clean test smoke IMAGE=gcc:14
#
# Requires Docker (use OrbStack on macOS: `orb start`).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

IMAGE="${IMAGE:-gcc:14}"
PHASE="phase1"
SUB="${1:?usage: $0 [phase1] <c11-ref|http2|doom> [targets...] [VAR=value...] OR $0 phase2 <zcc> [targets...]}"
shift || true

case "${SUB}" in
  phase1|phase2)
    PHASE="${SUB}"
    SUB="${1:?usage: $0 ${PHASE} <subproject> [targets...] [VAR=value...]}"
    shift || true
    ;;
esac

if [ ! -d "${ROOT}/${PHASE}/${SUB}" ]; then
  echo "error: ${PHASE}/${SUB} does not exist under ${ROOT}" >&2
  exit 2
fi
if [ "${PHASE}" = "phase1" ] && [ ! -f "${ROOT}/${PHASE}/${SUB}/Makefile" ]; then
  echo "error: ${PHASE}/${SUB}/Makefile not found (subproject not authored yet)" >&2
  exit 2
fi
if [ "${PHASE}" = "phase2" ] && [ ! -f "${ROOT}/${PHASE}/${SUB}/build.zig" ]; then
  echo "error: ${PHASE}/${SUB}/build.zig not found (subproject not authored yet)" >&2
  exit 2
fi

TARGETS=()
MAKE_VARS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --)         : ;;
    IMAGE=*)    IMAGE="${1#IMAGE=}" ;;
    *=*)        MAKE_VARS+=("$1") ;;
    *)          TARGETS+=("$1") ;;
  esac
  shift
done

if [ "${#TARGETS[@]}" -eq 0 ]; then
  if [ "${PHASE}" = "phase1" ]; then
    TARGETS=(print-platform all test)
  else
    TARGETS=(test smoke)
  fi
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required. on macOS: 'orb start' to launch OrbStack." >&2
  exit 3
fi

echo "==> image=${IMAGE}  subproject=${PHASE}/${SUB}  vars=[${MAKE_VARS[*]:-}]  targets=[${TARGETS[*]}]"

# Build a shell-safe command line so whitespace in make variables (e.g.
# CFLAGS='-O2 -g -fsanitize=address') survives the trip through docker.
if [ "${PHASE}" = "phase1" ]; then
  cmd="set -euxo pipefail; exec make"
  if [ "${#MAKE_VARS[@]}" -gt 0 ]; then
    for v in "${MAKE_VARS[@]}"; do
      cmd+=" $(printf '%q' "$v")"
    done
  fi
  for t in "${TARGETS[@]}"; do
    cmd+=" $(printf '%q' "$t")"
  done
else
  cmd='set -euxo pipefail; export PATH="/opt/zig:$PATH"; if ! command -v zig >/dev/null 2>&1; then apt-get update; apt-get install -y --no-install-recommends ca-certificates curl xz-utils; arch="$(uname -m)"; case "$arch" in x86_64|amd64) zig_arch="x86_64" ;; aarch64|arm64) zig_arch="aarch64" ;; *) echo "unsupported arch: $arch" >&2; exit 4 ;; esac; curl -fsSL "https://ziglang.org/download/0.16.0/zig-${zig_arch}-linux-0.16.0.tar.xz" -o /tmp/zig.tar.xz; mkdir -p /opt/zig; tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1; fi; zig version;'
  for t in "${TARGETS[@]}"; do
    case "${t}" in
      clean) cmd+=' rm -rf .zig-cache zig-out;' ;;
      all|build) cmd+=' zig build;' ;;
      test|smoke|fmt|lint) cmd+=" zig build $(printf '%q' "$t");" ;;
      *) echo "error: unsupported phase2 target: ${t}" >&2; exit 2 ;;
    esac
  done
fi

docker run --rm \
  -v "${ROOT}:/work" \
  -w "/work/${PHASE}/${SUB}" \
  "${IMAGE}" \
  bash -c "$cmd"
