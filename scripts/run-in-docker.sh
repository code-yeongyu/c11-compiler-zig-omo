#!/usr/bin/env bash
# scripts/run-in-docker.sh
#
# Reproduce the Phase-1 gcc-in-docker CI matrix locally. Same image,
# same volume layout, same make invocation as .github/workflows/phase1-c.yml.
#
# Usage:
#   scripts/run-in-docker.sh <subproject> [target...] [-- IMAGE=gcc:14] [IO=epoll|io_uring]
#
# Examples:
#   scripts/run-in-docker.sh c11-ref            # default: print-platform + all + test
#   scripts/run-in-docker.sh http2 all test smoke IO=io_uring
#   scripts/run-in-docker.sh doom all smoke -- IMAGE=gcc:13
#
# Requires Docker (use OrbStack on macOS: `orb start`).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

IMAGE="${IMAGE:-gcc:14}"
SUB="${1:?usage: $0 <c11-ref|http2|doom> [targets...] [VAR=value...]}"
shift || true

if [ ! -d "${ROOT}/phase1/${SUB}" ]; then
  echo "error: phase1/${SUB} does not exist under ${ROOT}" >&2
  exit 2
fi
if [ ! -f "${ROOT}/phase1/${SUB}/Makefile" ]; then
  echo "error: phase1/${SUB}/Makefile not found (subproject not authored yet)" >&2
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
  TARGETS=(print-platform all test)
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is required. on macOS: 'orb start' to launch OrbStack." >&2
  exit 3
fi

echo "==> image=${IMAGE}  subproject=phase1/${SUB}  vars=[${MAKE_VARS[*]:-}]  targets=[${TARGETS[*]}]"

docker run --rm \
  -v "${ROOT}:/work" \
  -w "/work/phase1/${SUB}" \
  "${IMAGE}" \
  bash -c "
    set -euxo pipefail
    for t in ${TARGETS[*]}; do
      make ${MAKE_VARS[*]:-} \"\$t\"
    done
  "
