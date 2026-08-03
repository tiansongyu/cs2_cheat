#!/bin/sh
set -eu

umask 077

usage() {
    printf 'Usage: %s <radar-domain> <room-id> [max-viewers]\n' "$0" >&2
    printf 'Example: %s radar.example.com team-a 20\n' "$0" >&2
    exit 2
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    usage
fi

domain=$1
room_id=$2
max_viewers=${3:-20}

if ! printf '%s\n' "$domain" | awk -F. '
    length($0) > 253 || NF < 2 { exit 1 }
    {
        for (label_index = 1; label_index <= NF; label_index++) {
            if (length($label_index) > 63 ||
                $label_index !~ /^([a-z0-9]|[a-z0-9][a-z0-9-]*[a-z0-9])$/) {
                exit 1
            }
        }
        if ($NF !~ /[a-z]/) { exit 1 }
    }
'; then
    printf 'Invalid domain: use a lowercase ASCII DNS hostname without scheme, port, or path.\n' >&2
    exit 2
fi

if ! printf '%s' "$room_id" | grep -Eq '^[A-Za-z0-9_-]{3,64}$'; then
    printf 'Invalid room id: expected 3-64 letters, digits, underscore, or hyphen.\n' >&2
    exit 2
fi

case "$max_viewers" in
    ''|*[!0-9]*|0|0*)
        printf 'Invalid max-viewers: expected an integer from 1 through 10000.\n' >&2
        exit 2
        ;;
esac
if [ "${#max_viewers}" -gt 5 ] ||
    [ "$max_viewers" -lt 1 ] || [ "$max_viewers" -gt 10000 ]; then
    printf 'Invalid max-viewers: expected an integer from 1 through 10000.\n' >&2
    exit 2
fi

if ! command -v openssl >/dev/null 2>&1; then
    printf 'openssl is required to generate high-entropy credentials.\n' >&2
    exit 1
fi

script_dir=$(
    CDPATH=''
    cd "$(dirname "$0")"
    pwd
)
secret_dir="$script_dir/secrets"
config_file="$secret_dir/relay-config.json"
credentials_file="$secret_dir/${room_id}-credentials.txt"

mkdir -p "$secret_dir"
chmod 0700 "$secret_dir"

if [ -e "$config_file" ] || [ -e "$credentials_file" ]; then
    printf 'Refusing to overwrite an existing config or credential file.\n' >&2
    printf 'Rotate credentials deliberately; do not regenerate them over a live room.\n' >&2
    exit 1
fi

producer_token=$(openssl rand -hex 32)
invite_token=$(openssl rand -hex 32)
producer_hash=$(printf '%s' "$producer_token" | openssl dgst -sha256 | awk '{print $NF}')
invite_hash=$(printf '%s' "$invite_token" | openssl dgst -sha256 | awk '{print $NF}')

tmp_config=$(mktemp "$secret_dir/.relay-config.XXXXXX")
tmp_credentials=$(mktemp "$secret_dir/.room-credentials.XXXXXX")
cleanup() {
    rm -f "$tmp_config" "$tmp_credentials"
}
trap cleanup EXIT HUP INT TERM

cat >"$tmp_config" <<EOF
{
  "listen": "0.0.0.0:8080",
  "publicOrigin": "https://$domain",
  "staticDir": "/srv/web-radar",
  "trustedProxyCIDRs": ["172.30.67.2/32"],
  "sessionTTLSeconds": 3600,
  "snapshotTTLMillis": 3000,
  "maxSnapshotAgeMillis": 10000,
  "maxFutureSkewMillis": 30000,
  "producerIdleTimeoutSeconds": 30,
  "maxSnapshotBytes": 524288,
  "maxPublishHz": 30,
  "publishBurst": 5,
  "loginAttemptsPerMinute": 10,
  "loginBurst": 5,
  "maxTrackedIPs": 10000,
  "maxSessions": 2048,
  "maxViewersPerSession": 2,
  "shutdownTimeoutSeconds": 10,
  "rooms": [
    {
      "id": "$room_id",
      "producerTokenSha256": "$producer_hash",
      "inviteTokenSha256": ["$invite_hash"],
      "maxViewers": $max_viewers
    }
  ]
}
EOF

cat >"$tmp_credentials" <<EOF
room=$room_id
producer_token=$producer_token
invite_token=$invite_token
EOF

# The operator changes the config owner to the Relay UID before starting
# Compose. Plaintext credentials remain owner-readable and are never mounted.
chmod 0600 "$tmp_config"
chmod 0600 "$tmp_credentials"
mv "$tmp_config" "$config_file"
mv "$tmp_credentials" "$credentials_file"
trap - EXIT HUP INT TERM

printf 'Created hash-only Relay config: %s\n' "$config_file"
printf 'Created one-time plaintext credentials: %s\n' "$credentials_file"
printf 'Before startup, chown the config to 65532:65532 and chmod it 0400.\n'
printf 'Move the plaintext values into a password manager, then securely delete that file.\n'
