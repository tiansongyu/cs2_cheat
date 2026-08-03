package config

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/auth"
)

var roomIDPattern = regexp.MustCompile(`^[A-Za-z0-9_-]{3,64}$`)

const maxConfigBytes = 1 << 20

type Config struct {
	Listen                     string       `json:"listen"`
	PublicOrigin               string       `json:"publicOrigin"`
	StaticDir                  string       `json:"staticDir,omitempty"`
	TLSCertFile                string       `json:"tlsCertFile,omitempty"`
	TLSKeyFile                 string       `json:"tlsKeyFile,omitempty"`
	TrustedProxyCIDRs          []string     `json:"trustedProxyCIDRs,omitempty"`
	SessionTTLSeconds          int          `json:"sessionTTLSeconds"`
	SnapshotTTLMillis          int          `json:"snapshotTTLMillis"`
	MaxSnapshotAgeMillis       int          `json:"maxSnapshotAgeMillis"`
	MaxFutureSkewMillis        int          `json:"maxFutureSkewMillis"`
	ProducerIdleTimeoutSeconds int          `json:"producerIdleTimeoutSeconds"`
	MaxSnapshotBytes           int64        `json:"maxSnapshotBytes"`
	MaxPublishHz               float64      `json:"maxPublishHz"`
	PublishBurst               int          `json:"publishBurst"`
	LoginAttemptsPerMinute     float64      `json:"loginAttemptsPerMinute"`
	LoginBurst                 int          `json:"loginBurst"`
	MaxTrackedIPs              int          `json:"maxTrackedIPs"`
	MaxSessions                int          `json:"maxSessions"`
	MaxViewersPerSession       int          `json:"maxViewersPerSession"`
	ShutdownTimeoutSeconds     int          `json:"shutdownTimeoutSeconds"`
	EnableMetrics              bool         `json:"enableMetrics,omitempty"`
	AllowInsecureDevelopment   bool         `json:"allowInsecureDevelopment,omitempty"`
	Rooms                      []RoomConfig `json:"rooms"`
}

type RoomConfig struct {
	ID                  string   `json:"id"`
	ProducerTokenSHA256 string   `json:"producerTokenSha256"`
	InviteTokenSHA256   []string `json:"inviteTokenSha256"`
	MaxViewers          int      `json:"maxViewers"`
}

func Default() Config {
	return Config{
		Listen:                     "127.0.0.1:8080",
		SessionTTLSeconds:          3600,
		SnapshotTTLMillis:          3000,
		MaxSnapshotAgeMillis:       10000,
		MaxFutureSkewMillis:        30000,
		ProducerIdleTimeoutSeconds: 30,
		MaxSnapshotBytes:           512 * 1024,
		MaxPublishHz:               30,
		PublishBurst:               5,
		LoginAttemptsPerMinute:     10,
		LoginBurst:                 5,
		MaxTrackedIPs:              10000,
		MaxSessions:                10000,
		MaxViewersPerSession:       2,
		ShutdownTimeoutSeconds:     10,
	}
}

func Load(path string) (Config, error) {
	file, err := os.Open(path)
	if err != nil {
		return Config{}, err
	}
	defer file.Close()

	payload, err := io.ReadAll(io.LimitReader(file, maxConfigBytes+1))
	if err != nil {
		return Config{}, fmt.Errorf("read config: %w", err)
	}
	if len(payload) > maxConfigBytes {
		return Config{}, fmt.Errorf("config exceeds %d bytes", maxConfigBytes)
	}

	cfg := Default()
	decoder := json.NewDecoder(bytes.NewReader(payload))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&cfg); err != nil {
		return Config{}, fmt.Errorf("decode config: %w", err)
	}
	if err := ensureJSONEOF(decoder); err != nil {
		return Config{}, err
	}
	if err := cfg.Validate(); err != nil {
		return Config{}, err
	}
	return cfg, nil
}

func ensureJSONEOF(decoder *json.Decoder) error {
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return errors.New("config must contain one JSON object")
		}
		return fmt.Errorf("decode trailing config data: %w", err)
	}
	return nil
}

func (c Config) Validate() error {
	if strings.TrimSpace(c.Listen) == "" {
		return errors.New("listen is required")
	}
	if _, _, err := net.SplitHostPort(c.Listen); err != nil {
		return fmt.Errorf("listen: %w", err)
	}
	if err := validateOrigin(c.PublicOrigin, c.AllowInsecureDevelopment); err != nil {
		return fmt.Errorf("publicOrigin: %w", err)
	}
	if (c.TLSCertFile == "") != (c.TLSKeyFile == "") {
		return errors.New("tlsCertFile and tlsKeyFile must be configured together")
	}
	for _, cidr := range c.TrustedProxyCIDRs {
		if _, _, err := net.ParseCIDR(cidr); err != nil {
			return fmt.Errorf("trustedProxyCIDRs contains %q: %w", cidr, err)
		}
	}
	if c.SessionTTLSeconds < 60 || c.SessionTTLSeconds > 86400 {
		return errors.New("sessionTTLSeconds must be between 60 and 86400")
	}
	if c.SnapshotTTLMillis < 250 || c.SnapshotTTLMillis > 10000 {
		return errors.New("snapshotTTLMillis must be between 250 and 10000")
	}
	if c.MaxSnapshotAgeMillis < 1000 || c.MaxSnapshotAgeMillis > 60000 {
		return errors.New("maxSnapshotAgeMillis must be between 1000 and 60000")
	}
	if c.MaxFutureSkewMillis < 1000 || c.MaxFutureSkewMillis > 300000 {
		return errors.New("maxFutureSkewMillis must be between 1000 and 300000")
	}
	if c.ProducerIdleTimeoutSeconds < 5 || c.ProducerIdleTimeoutSeconds > 300 {
		return errors.New("producerIdleTimeoutSeconds must be between 5 and 300")
	}
	if c.MaxSnapshotBytes < 4096 || c.MaxSnapshotBytes > 4*1024*1024 {
		return errors.New("maxSnapshotBytes must be between 4096 and 4194304")
	}
	if c.MaxPublishHz < 1 || c.MaxPublishHz > 120 {
		return errors.New("maxPublishHz must be between 1 and 120")
	}
	if c.PublishBurst < 1 || c.PublishBurst > 120 {
		return errors.New("publishBurst must be between 1 and 120")
	}
	if c.LoginAttemptsPerMinute < 1 || c.LoginAttemptsPerMinute > 600 {
		return errors.New("loginAttemptsPerMinute must be between 1 and 600")
	}
	if c.LoginBurst < 1 || c.LoginBurst > 100 {
		return errors.New("loginBurst must be between 1 and 100")
	}
	if c.MaxTrackedIPs < 100 || c.MaxTrackedIPs > 1000000 {
		return errors.New("maxTrackedIPs must be between 100 and 1000000")
	}
	if c.MaxSessions < 1 || c.MaxSessions > 1000000 {
		return errors.New("maxSessions must be between 1 and 1000000")
	}
	if c.MaxViewersPerSession < 1 || c.MaxViewersPerSession > 8 {
		return errors.New("maxViewersPerSession must be between 1 and 8")
	}
	if c.ShutdownTimeoutSeconds < 1 || c.ShutdownTimeoutSeconds > 60 {
		return errors.New("shutdownTimeoutSeconds must be between 1 and 60")
	}
	if len(c.Rooms) == 0 || len(c.Rooms) > 1000 {
		return errors.New("rooms must contain between 1 and 1000 rooms")
	}
	seen := make(map[string]struct{}, len(c.Rooms))
	seenTokenHashes := make(map[[32]byte]string)
	for i, room := range c.Rooms {
		if !roomIDPattern.MatchString(room.ID) {
			return fmt.Errorf("rooms[%d].id must match %s", i, roomIDPattern)
		}
		if _, exists := seen[room.ID]; exists {
			return fmt.Errorf("duplicate room id %q", room.ID)
		}
		seen[room.ID] = struct{}{}
		producerHash, err := auth.ParseHexSum(room.ProducerTokenSHA256)
		if err != nil {
			return fmt.Errorf("rooms[%d].producerTokenSha256: %w", i, err)
		}
		if previous, reused := seenTokenHashes[producerHash]; reused {
			return fmt.Errorf("rooms[%d].producerTokenSha256 reuses the token digest from %s", i, previous)
		}
		seenTokenHashes[producerHash] = fmt.Sprintf("room %q producer", room.ID)
		if len(room.InviteTokenSHA256) == 0 || len(room.InviteTokenSHA256) > 16 {
			return fmt.Errorf("rooms[%d].inviteTokenSha256 must contain 1 to 16 hashes", i)
		}
		for j, hash := range room.InviteTokenSHA256 {
			inviteHash, err := auth.ParseHexSum(hash)
			if err != nil {
				return fmt.Errorf("rooms[%d].inviteTokenSha256[%d]: %w", i, j, err)
			}
			if previous, reused := seenTokenHashes[inviteHash]; reused {
				return fmt.Errorf("rooms[%d].inviteTokenSha256[%d] reuses the token digest from %s", i, j, previous)
			}
			seenTokenHashes[inviteHash] = fmt.Sprintf("room %q invite %d", room.ID, j)
		}
		if room.MaxViewers < 1 || room.MaxViewers > 10000 {
			return fmt.Errorf("rooms[%d].maxViewers must be between 1 and 10000", i)
		}
	}
	return nil
}

func validateOrigin(value string, allowInsecure bool) error {
	parsed, err := url.Parse(value)
	if err != nil {
		return err
	}
	if parsed.Scheme == "" || parsed.Host == "" || parsed.User != nil || parsed.RawQuery != "" || parsed.Fragment != "" || (parsed.Path != "" && parsed.Path != "/") {
		return errors.New("must be an absolute origin without path, credentials, query, or fragment")
	}
	canonical := parsed.Scheme + "://" + parsed.Host
	if value != canonical {
		return fmt.Errorf("must use canonical form %q", canonical)
	}
	if parsed.Scheme != "https" && !(allowInsecure && parsed.Scheme == "http") {
		return errors.New("must use https (http is allowed only for explicit development mode)")
	}
	port := parsed.Port()
	if strings.HasSuffix(parsed.Host, ":") {
		return errors.New("host must not contain an empty port")
	}
	if port != "" {
		numericPort, conversionErr := strconv.Atoi(port)
		if conversionErr != nil || numericPort < 1 || numericPort > 65535 || strconv.Itoa(numericPort) != port {
			return errors.New("port must use canonical decimal form from 1 through 65535")
		}
	}
	if (parsed.Scheme == "https" && port == "443") || (parsed.Scheme == "http" && port == "80") {
		return errors.New("must omit the scheme's default port because browsers omit it from Origin")
	}
	if err := validateBrowserHost(parsed.Hostname()); err != nil {
		return err
	}
	return nil
}

func validateBrowserHost(host string) error {
	if host == "" || host != strings.ToLower(host) {
		return errors.New("host must be lowercase")
	}
	if strings.HasSuffix(host, ".") {
		return errors.New("host must not have a trailing dot")
	}
	if strings.ContainsFunc(host, func(r rune) bool { return r > 0x7f }) {
		return errors.New("host must be ASCII; use lowercase punycode for internationalized domains")
	}
	if parsedIP := net.ParseIP(host); parsedIP != nil {
		if host != parsedIP.String() {
			return errors.New("IP host must use its canonical browser form")
		}
		return nil
	}
	if len(host) > 253 {
		return errors.New("DNS host is too long")
	}
	for _, label := range strings.Split(host, ".") {
		if len(label) == 0 || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return errors.New("DNS host contains an invalid label")
		}
		for _, character := range label {
			if (character < 'a' || character > 'z') && (character < '0' || character > '9') && character != '-' {
				return errors.New("DNS host must contain only lowercase ASCII letters, digits, dots, and hyphens")
			}
		}
	}
	return nil
}

func (c Config) SessionTTL() time.Duration {
	return time.Duration(c.SessionTTLSeconds) * time.Second
}

func (c Config) SnapshotTTL() time.Duration {
	return time.Duration(c.SnapshotTTLMillis) * time.Millisecond
}

func (c Config) MaxSnapshotAge() time.Duration {
	return time.Duration(c.MaxSnapshotAgeMillis) * time.Millisecond
}

func (c Config) MaxFutureSkew() time.Duration {
	return time.Duration(c.MaxFutureSkewMillis) * time.Millisecond
}

func (c Config) ProducerIdleTimeout() time.Duration {
	return time.Duration(c.ProducerIdleTimeoutSeconds) * time.Second
}

func (c Config) ShutdownTimeout() time.Duration {
	return time.Duration(c.ShutdownTimeoutSeconds) * time.Second
}
