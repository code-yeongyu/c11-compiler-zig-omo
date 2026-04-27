#!/usr/bin/env bash
set -euo pipefail

if ! curl --help all 2>/dev/null | grep -q -- '--http2-prior-knowledge'; then
  echo "curl lacks --http2-prior-knowledge; cannot run h2c smoke on this host"
  exit 0
fi

ready_file="$(mktemp -t h2d-ready.XXXXXX)"
body_file="$(mktemp -t h2d-body.XXXXXX)"
rm -f "$ready_file"

./build/h2d --host 127.0.0.1 --port 0 --ready-file "$ready_file" --max-connections 1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true; rm -f "$ready_file" "$body_file"' EXIT

tries=0
while [ ! -s "$ready_file" ] && [ "$tries" -lt 1000000 ]; do
  tries=$((tries + 1))
done

if [ ! -s "$ready_file" ]; then
  echo "server did not publish ready file" >&2
  exit 1
fi

port="$(tr -d '\r\n' < "$ready_file")"
curl --max-time 5 --http2-prior-knowledge -sS -o "$body_file" "http://127.0.0.1:${port}/"

if ! cmp -s "$body_file" - <<'EXPECTED'
/
EXPECTED
then
  echo "unexpected response body:" >&2
  cat "$body_file" >&2
  exit 1
fi

wait "$server_pid"
echo "smoke: ok"
