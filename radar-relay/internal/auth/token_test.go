package auth

import (
	"encoding/base64"
	"testing"
)

func TestGenerateTokenAndHash(t *testing.T) {
	token, err := GenerateToken()
	if err != nil {
		t.Fatal(err)
	}
	raw, err := base64.RawURLEncoding.DecodeString(token)
	if err != nil {
		t.Fatalf("token is not URL-safe base64: %v", err)
	}
	if len(raw) != 32 {
		t.Fatalf("got %d entropy bytes, want 32", len(raw))
	}
	parsed, err := ParseHexSum(HexSum(token))
	if err != nil {
		t.Fatal(err)
	}
	if !Matches(token, parsed) || Matches(token+"x", parsed) {
		t.Fatal("constant-time digest comparison returned the wrong result")
	}
}

func TestMatchesAny(t *testing.T) {
	hashes := [][32]byte{Sum("first-token-that-is-long-enough"), Sum("second-token-that-is-long-enough")}
	if !MatchesAny("second-token-that-is-long-enough", hashes) {
		t.Fatal("expected rotating invite set to match")
	}
	if MatchesAny("unknown-token-that-is-long-enough", hashes) {
		t.Fatal("unexpected invite match")
	}
}

func TestValidPresentedToken(t *testing.T) {
	if !ValidPresentedToken("abcdefghijklmnopqrstuvwxyz012345") {
		t.Fatal("valid token rejected")
	}
	for _, value := range []string{"short", "abcdefghijklmnopqrstuvw\nxyz", "abcdefghijklmnopqrstuvw xyz"} {
		if ValidPresentedToken(value) {
			t.Fatalf("invalid token %q accepted", value)
		}
	}
}
