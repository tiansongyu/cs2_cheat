package relay

import "testing"

const validSnapshot = `{"v":1,"type":"snapshot","protocolVersion":1,"seq":7,"capturedAtMs":123,"map":{},"players":[],"bomb":{}}`

func TestValidateSnapshot(t *testing.T) {
	if _, err := validateSnapshot([]byte(validSnapshot)); err != nil {
		t.Fatalf("valid snapshot rejected: %v", err)
	}
	invalid := []string{
		`{"v":1,"type":"hello","protocolVersion":1,"seq":1,"capturedAtMs":1,"map":{},"players":[],"bomb":{}}`,
		`{"v":2,"type":"snapshot","protocolVersion":1,"seq":1,"capturedAtMs":1,"map":{},"players":[],"bomb":{}}`,
		`{"v":1,"type":"snapshot","protocolVersion":1,"capturedAtMs":1,"map":{},"players":[],"bomb":{}}`,
		`{"v":1,"type":"snapshot","protocolVersion":1,"seq":1,"capturedAtMs":1,"map":[],"players":[],"bomb":{}}`,
		validSnapshot + `{}`,
		`not-json`,
	}
	for _, payload := range invalid {
		if _, err := validateSnapshot([]byte(payload)); err == nil {
			t.Fatalf("invalid snapshot accepted: %s", payload)
		}
	}
	if _, err := validateSnapshot([]byte{'{', '"', 0xff, '"', '}'}); err == nil {
		t.Fatal("invalid UTF-8 snapshot accepted")
	}
}
