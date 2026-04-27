#!/usr/bin/env bash
# scripts/run-in-docker-phase2.sh
#
# Reproduce the Phase-2 zcc Docker CI cell locally.
#
# Usage:
#   scripts/run-in-docker-phase2.sh zcc [target...] [IMAGE=gcc:14]
#
# Examples:
#   scripts/run-in-docker-phase2.sh zcc clean test smoke
#   IMAGE=gcc:14 scripts/run-in-docker-phase2.sh zcc clean test smoke
#
# Requires Docker (use OrbStack on macOS: `orb start`).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

IMAGE="${IMAGE:-gcc:14}"
SUB="${1:?usage: $0 <zcc> [targets...] [IMAGE=gcc:14]}"
shift || true

if [ ! -d "${ROOT}/phase2/${SUB}" ]; then
  echo "error: phase2/${SUB} does not exist under ${ROOT}" >&2
  exit 2
fi
if [ ! -f "${ROOT}/phase2/${SUB}/build.zig" ]; then
  echo "error: phase2/${SUB}/build.zig not found (subproject not authored yet)" >&2
  exit 2
fi

TARGETS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --)      : ;;
    IMAGE=*) IMAGE="${1#IMAGE=}" ;;
    *=*)     echo "error: phase2 docker runner does not accept make vars: $1" >&2; exit 2 ;;
    *)       TARGETS+=("$1") ;;
  esac
  shift
done

if [ "${#TARGETS[@]}" -eq 0 ]; then
  TARGETS=(test smoke)
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required. on macOS: 'orb start' to launch OrbStack." >&2
  exit 3
fi

echo "==> image=${IMAGE}  subproject=phase2/${SUB}  targets=[${TARGETS[*]}]"

cmd='set -euxo pipefail; export PATH="/opt/zig:$PATH"; if ! command -v zig >/dev/null 2>&1; then apt-get update; apt-get install -y --no-install-recommends ca-certificates curl xz-utils; arch="$(uname -m)"; case "$arch" in x86_64|amd64) zig_arch="x86_64" ;; aarch64|arm64) zig_arch="aarch64" ;; *) echo "unsupported arch: $arch" >&2; exit 4 ;; esac; curl -fsSL "https://ziglang.org/download/0.16.0/zig-${zig_arch}-linux-0.16.0.tar.xz" -o /tmp/zig.tar.xz; mkdir -p /opt/zig; tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1; fi; zig version;'
for target in "${TARGETS[@]}"; do
  case "${target}" in
    clean) cmd+=' rm -rf .zig-cache zig-out;' ;;
    all|build) cmd+=' zig build;' ;;
    test|smoke|fmt|lint) cmd+=" zig build $(printf '%q' "$target");" ;;
    *) echo "error: unsupported phase2 target: ${target}" >&2; exit 2 ;;
  esac
done

docker run --rm \
  -v "${ROOT}:/work" \
  -w "/work/phase2/${SUB}" \
  "${IMAGE}" \
  bash -c "$cmd"
