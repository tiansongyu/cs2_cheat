package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"strings"
)

const tokenBytes = 32

// GenerateToken returns a URL-safe token with 256 bits of entropy.
func GenerateToken() (string, error) {
	raw := make([]byte, tokenBytes)
	if _, err := rand.Read(raw); err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(raw), nil
}

func Sum(token string) [sha256.Size]byte {
	return sha256.Sum256([]byte(token))
}

func HexSum(token string) string {
	sum := Sum(token)
	return hex.EncodeToString(sum[:])
}

func ParseHexSum(value string) ([sha256.Size]byte, error) {
	var result [sha256.Size]byte
	decoded, err := hex.DecodeString(value)
	if err != nil || len(decoded) != sha256.Size {
		return result, errors.New("must be exactly 64 hexadecimal characters")
	}
	copy(result[:], decoded)
	return result, nil
}

// Matches compares a presented high-entropy token to its configured SHA-256
// digest without leaking a matching prefix through the comparison itself.
func Matches(token string, expected [sha256.Size]byte) bool {
	presented := Sum(token)
	return subtle.ConstantTimeCompare(presented[:], expected[:]) == 1
}

// MatchesAny always examines every configured digest. This avoids revealing
// which invite in a rotation set matched through an early return.
func MatchesAny(token string, expected [][sha256.Size]byte) bool {
	presented := Sum(token)
	matched := 0
	for i := range expected {
		matched |= subtle.ConstantTimeCompare(presented[:], expected[i][:])
	}
	return matched == 1
}

func ValidPresentedToken(token string) bool {
	if len(token) < 24 || len(token) > 512 {
		return false
	}
	return !strings.ContainsFunc(token, func(r rune) bool {
		return r <= ' ' || r == 0x7f
	})
}
