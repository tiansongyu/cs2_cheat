package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/auth"
)

func validConfig() Config {
	cfg := Default()
	cfg.PublicOrigin = "https://radar.example.test"
	cfg.Rooms = []RoomConfig{{
		ID:                  "room_one",
		ProducerTokenSHA256: auth.HexSum("producer-token-with-enough-entropy"),
		InviteTokenSHA256:   []string{auth.HexSum("invite-token-with-enough-entropy")},
		MaxViewers:          8,
	}}
	return cfg
}

func TestLoadRejectsConfigLargerThanLimit(t *testing.T) {
	path := filepath.Join(t.TempDir(), "oversized.json")
	payload := append([]byte(`{}`), make([]byte, maxConfigBytes)...)
	if err := os.WriteFile(path, payload, 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(path); err == nil || !strings.Contains(err.Error(), "exceeds") {
		t.Fatalf("Load oversized config error = %v, want size-limit error", err)
	}
}

func TestValidateAcceptsProductionConfig(t *testing.T) {
	if err := validConfig().Validate(); err != nil {
		t.Fatal(err)
	}
}

func TestValidateRejectsUnsafeOriginsAndRooms(t *testing.T) {
	tests := []struct {
		name   string
		mutate func(*Config)
		want   string
	}{
		{"http origin", func(c *Config) { c.PublicOrigin = "http://radar.example.test" }, "https"},
		{"origin path", func(c *Config) { c.PublicOrigin += "/login" }, "origin"},
		{"uppercase host", func(c *Config) { c.PublicOrigin = "https://Radar.Example.test" }, "lowercase"},
		{"default port", func(c *Config) { c.PublicOrigin = "https://radar.example.test:443" }, "default port"},
		{"noncanonical port", func(c *Config) { c.PublicOrigin = "https://radar.example.test:0443" }, "canonical decimal"},
		{"empty port", func(c *Config) { c.PublicOrigin = "https://radar.example.test:" }, "empty port"},
		{"unicode host", func(c *Config) { c.PublicOrigin = "https://雷达.example" }, "ASCII"},
		{"trailing dot", func(c *Config) { c.PublicOrigin = "https://radar.example.test." }, "trailing dot"},
		{"duplicate room", func(c *Config) { c.Rooms = append(c.Rooms, c.Rooms[0]) }, "duplicate"},
		{"bad room id", func(c *Config) { c.Rooms[0].ID = "bad room" }, "must match"},
		{"bad digest", func(c *Config) { c.Rooms[0].ProducerTokenSHA256 = "plain-secret" }, "64 hexadecimal"},
		{"role reuse", func(c *Config) { c.Rooms[0].InviteTokenSHA256[0] = c.Rooms[0].ProducerTokenSHA256 }, "reuses"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			cfg := validConfig()
			test.mutate(&cfg)
			err := cfg.Validate()
			if err == nil || !strings.Contains(err.Error(), test.want) {
				t.Fatalf("got error %v, want substring %q", err, test.want)
			}
		})
	}
}
