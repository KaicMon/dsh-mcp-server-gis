#!/usr/bin/env bash
set -euo pipefail

test_binary="$1"
port="$2"
temp_dir=$(mktemp -d)
cleanup() {
    if test -f "$temp_dir/redis.pid"; then
        kill "$(cat "$temp_dir/redis.pid")" 2>/dev/null || true
    fi
    rm -f -- "$temp_dir/redis.pid" "$temp_dir/redis.log"
    rmdir -- "$temp_dir" 2>/dev/null || true
}
trap cleanup EXIT

redis-server --bind 127.0.0.1 --port "$port" --save '' --appendonly no \
    --daemonize yes --dir "$temp_dir" --pidfile "$temp_dir/redis.pid" \
    --logfile "$temp_dir/redis.log"
for _ in 1 2 3 4 5; do
    redis-cli -h 127.0.0.1 -p "$port" ping 2>/dev/null | grep -q PONG && break
    sleep 1
done
"$test_binary" "$port"
