package relay

import (
	"bytes"
	"encoding/json"
	"errors"
	"io"
	"time"
	"unicode/utf8"
)

type snapshotEnvelope struct {
	Version         int             `json:"v"`
	Type            string          `json:"type"`
	ProtocolVersion int             `json:"protocolVersion"`
	Sequence        *uint64         `json:"seq"`
	CapturedAtMs    *int64          `json:"capturedAtMs"`
	Map             json.RawMessage `json:"map"`
	Players         json.RawMessage `json:"players"`
	Bomb            json.RawMessage `json:"bomb"`
}

func validateSnapshot(payload []byte) (time.Time, error) {
	if !utf8.Valid(payload) {
		return time.Time{}, errors.New("snapshot must be valid UTF-8")
	}
	decoder := json.NewDecoder(bytes.NewReader(payload))
	var envelope snapshotEnvelope
	if err := decoder.Decode(&envelope); err != nil {
		return time.Time{}, errors.New("invalid JSON snapshot")
	}
	var trailing any
	if err := decoder.Decode(&trailing); !errors.Is(err, io.EOF) {
		return time.Time{}, errors.New("snapshot must contain exactly one JSON value")
	}
	if envelope.Version != 1 || envelope.Type != "snapshot" {
		return time.Time{}, errors.New("only v1 snapshot messages are accepted")
	}
	if envelope.ProtocolVersion != 1 {
		return time.Time{}, errors.New("protocolVersion must be 1")
	}
	if envelope.Sequence == nil || envelope.CapturedAtMs == nil {
		return time.Time{}, errors.New("snapshot sequence and capture time are required")
	}
	if *envelope.CapturedAtMs < 0 {
		return time.Time{}, errors.New("snapshot capture time must be non-negative")
	}
	if !isJSONObject(envelope.Map) || !isJSONArray(envelope.Players) || !isJSONObject(envelope.Bomb) {
		return time.Time{}, errors.New("snapshot map, players, and bomb have invalid types")
	}
	return time.UnixMilli(*envelope.CapturedAtMs), nil
}

func isJSONObject(raw json.RawMessage) bool {
	trimmed := bytes.TrimSpace(raw)
	return len(trimmed) >= 2 && trimmed[0] == '{' && trimmed[len(trimmed)-1] == '}'
}

func isJSONArray(raw json.RawMessage) bool {
	trimmed := bytes.TrimSpace(raw)
	return len(trimmed) >= 2 && trimmed[0] == '[' && trimmed[len(trimmed)-1] == ']'
}
