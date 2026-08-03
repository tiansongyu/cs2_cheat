#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ -z "${RADAR_RELAY_BIN:-}" ]; then
    printf 'Usage: RADAR_RELAY_BIN=/path/to/radar-relay %s /path/to/generate-room-config.sh\n' "$0" >&2
    exit 2
fi

source_generator=$1
if [ ! -x "$source_generator" ] || [ ! -x "$RADAR_RELAY_BIN" ]; then
    printf 'Generator and RADAR_RELAY_BIN must both be executable.\n' >&2
    exit 2
fi

test_root=$(mktemp -d "${TMPDIR:-/tmp}/radar-generator-test.XXXXXX")
cleanup() {
    rm -r "$test_root"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

generator="$test_root/generate-room-config.sh"
cp "$source_generator" "$generator"
chmod 0700 "$generator"

"$generator" radar.example.test test_room 7 >"$test_root/generator.out"
config="$test_root/secrets/relay-config.json"
credentials="$test_root/secrets/test_room-credentials.txt"

[ "$(stat -c '%a' "$test_root/secrets")" = 700 ]
[ "$(stat -c '%a' "$config")" = 600 ]
[ "$(stat -c '%a' "$credentials")" = 600 ]

producer_token=$(awk -F= '$1 == "producer_token" { print substr($0, length($1) + 2) }' "$credentials")
invite_token=$(awk -F= '$1 == "invite_token" { print substr($0, length($1) + 2) }' "$credentials")
producer_hash=$(printf '%s' "$producer_token" | openssl dgst -sha256 | awk '{print $NF}')
invite_hash=$(printf '%s' "$invite_token" | openssl dgst -sha256 | awk '{print $NF}')

for token in "$producer_token" "$invite_token"; do
    printf '%s\n' "$token" | grep -Eq '^[0-9a-f]{64}$'
done
if [ "$producer_token" = "$invite_token" ]; then
    printf 'Producer and invite credentials are not independent.\n' >&2
    exit 1
fi
grep -Fq '"enableMetrics": true' "$config"
grep -Fq "\"producerTokenSha256\": \"$producer_hash\"" "$config"
grep -Fq "\"inviteTokenSha256\": [\"$invite_hash\"]" "$config"
if grep -Fq "$producer_token" "$config" || grep -Fq "$invite_token" "$config"; then
    printf 'Hash-only config contains a plaintext credential.\n' >&2
    exit 1
fi

"$RADAR_RELAY_BIN" check-config -config "$config" \
    -origin https://radar.example.test >"$test_root/check.out"

if "$generator" radar.example.test other_room 7 >"$test_root/overwrite.out" 2>&1; then
    printf 'Generator overwrote an existing config.\n' >&2
    exit 1
fi

bad_domain=$(printf 'radar.example.test\ninjected')
if "$generator" "$bad_domain" other_room 7 >"$test_root/domain.out" 2>&1; then
    printf 'Generator accepted a domain containing a newline.\n' >&2
    exit 1
fi
bad_room=$(printf 'other_room\ninjected')
if "$generator" radar.example.test "$bad_room" 7 >"$test_root/room.out" 2>&1; then
    printf 'Generator accepted a room containing a newline.\n' >&2
    exit 1
fi

mkdir "$test_root/secrets/.generate-room-config.lock"
if "$generator" radar.example.test locked_room 7 >"$test_root/lock.out" 2>&1; then
    printf 'Generator ignored its concurrency lock.\n' >&2
    exit 1
fi
grep -Fq 'Another generator may be running' "$test_root/lock.out"

printf 'public relay generator tests passed\n'
