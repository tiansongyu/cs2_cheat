package relay

import (
	"bytes"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/auth"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/config"
)

const (
	testOrigin        = "https://radar.example.test"
	testRoom          = "test_room"
	testProducerToken = "producer-token-with-at-least-32-chars"
	testInviteToken   = "invite-token-with-at-least-32-characters"
)

type relayFixture struct {
	server *Server
	http   *httptest.Server
	dialer *websocket.Dialer
}

func newRelayFixture(t *testing.T, mutate func(*config.Config)) *relayFixture {
	t.Helper()
	cfg := config.Default()
	cfg.PublicOrigin = testOrigin
	cfg.SnapshotTTLMillis = 250
	cfg.Rooms = []config.RoomConfig{{
		ID:                  testRoom,
		ProducerTokenSHA256: auth.HexSum(testProducerToken),
		InviteTokenSHA256:   []string{auth.HexSum(testInviteToken)},
		MaxViewers:          2,
	}}
	if mutate != nil {
		mutate(&cfg)
	}
	server, err := New(cfg, nil)
	if err != nil {
		t.Fatal(err)
	}
	httpServer := httptest.NewTLSServer(server.Handler())
	fixture := &relayFixture{
		server: server,
		http:   httpServer,
		dialer: &websocket.Dialer{HandshakeTimeout: time.Second, TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}, // test server only
	}
	t.Cleanup(func() {
		server.Close()
		httpServer.Close()
	})
	return fixture
}

func (f *relayFixture) wsURL(path string) string {
	return "wss" + strings.TrimPrefix(f.http.URL, "https") + path
}

func (f *relayFixture) createSession(t *testing.T) *http.Cookie {
	t.Helper()
	payload, _ := json.Marshal(map[string]string{"room": testRoom, "inviteToken": testInviteToken})
	request, _ := http.NewRequest(http.MethodPost, f.http.URL+"/api/v1/session", bytes.NewReader(payload))
	request.Header.Set("Origin", testOrigin)
	request.Header.Set("Content-Type", "application/json")
	response, err := f.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(response.Body)
		t.Fatalf("session login returned %d: %s", response.StatusCode, body)
	}
	cookies := response.Cookies()
	if len(cookies) != 1 {
		t.Fatalf("got %d cookies, want 1", len(cookies))
	}
	return cookies[0]
}

func (f *relayFixture) dialViewer(t *testing.T, cookie *http.Cookie) *websocket.Conn {
	t.Helper()
	header := http.Header{"Origin": []string{testOrigin}, "Cookie": []string{cookie.String()}}
	conn, response, err := f.dialer.Dial(f.wsURL("/api/v1/stream"), header)
	if err != nil {
		if response != nil {
			defer response.Body.Close()
		}
		t.Fatalf("viewer dial failed: %v", err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return conn
}

func (f *relayFixture) dialProducer(t *testing.T) *websocket.Conn {
	t.Helper()
	header := http.Header{
		"Authorization": []string{"Bearer " + testProducerToken},
		"X-Radar-Room":  []string{testRoom},
	}
	conn, response, err := f.dialer.Dial(f.wsURL("/api/v1/publish"), header)
	if err != nil {
		if response != nil {
			defer response.Body.Close()
		}
		t.Fatalf("producer dial failed: %v", err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return conn
}

func snapshotAt(capturedAt time.Time, sequence int) string {
	return fmt.Sprintf(`{"v":1,"type":"snapshot","protocolVersion":1,"seq":%d,"capturedAtMs":%d,"map":{},"players":[],"bomb":{}}`, sequence, capturedAt.UnixMilli())
}

func TestSessionSecurityAndNoOriginProbe(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	payload := []byte(`{"room":"test_room","inviteToken":"invite-token-with-at-least-32-characters"}`)
	request, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
	request.Header.Set("Origin", "https://evil.example")
	request.Header.Set("Content-Type", "application/json")
	response, err := fixture.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusForbidden {
		t.Fatalf("cross-origin login got %d, want 403", response.StatusCode)
	}
	duplicateOrigin, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
	duplicateOrigin.Header.Add("Origin", testOrigin)
	duplicateOrigin.Header.Add("Origin", "https://evil.example")
	duplicateOrigin.Header.Set("Content-Type", "application/json")
	response, err = fixture.http.Client().Do(duplicateOrigin)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusForbidden {
		t.Fatalf("duplicate Origin got %d, want 403", response.StatusCode)
	}
	queryResponse, err := fixture.http.Client().Get(fixture.http.URL + "/api/v1/session?token=must-not-be-accepted")
	if err != nil {
		t.Fatal(err)
	}
	queryResponse.Body.Close()
	if queryResponse.StatusCode != http.StatusBadRequest {
		t.Fatalf("API query got %d, want 400", queryResponse.StatusCode)
	}

	cookie := fixture.createSession(t)
	if !cookie.Secure || !cookie.HttpOnly || cookie.SameSite != http.SameSiteStrictMode || cookie.Path != "/" {
		t.Fatalf("unsafe session cookie attributes: %#v", cookie)
	}
	probe, _ := http.NewRequest(http.MethodGet, fixture.http.URL+"/api/v1/session", nil)
	probe.AddCookie(cookie)
	// Browsers do not normally attach Origin to same-origin GET requests.
	probeResponse, err := fixture.http.Client().Do(probe)
	if err != nil {
		t.Fatal(err)
	}
	defer probeResponse.Body.Close()
	if probeResponse.StatusCode != http.StatusOK {
		t.Fatalf("authenticated no-Origin probe got %d", probeResponse.StatusCode)
	}
	if probeResponse.Header.Get("Cache-Control") != "no-store" || probeResponse.Header.Get("Content-Security-Policy") == "" {
		t.Fatal("security headers are missing")
	}
}

func TestAuthenticationFailures(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	payload := []byte(`{"room":"test_room","inviteToken":"wrong-invite-token-with-32-characters"}`)
	request, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
	request.Header.Set("Origin", testOrigin)
	request.Header.Set("Content-Type", "application/json")
	response, err := fixture.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusUnauthorized {
		t.Fatalf("wrong invite returned %d, want 401", response.StatusCode)
	}

	conn, wsResponse, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/publish"), http.Header{
		"Authorization": []string{"Bearer wrong-producer-token-with-32-characters"},
		"X-Radar-Room":  []string{testRoom},
	})
	if conn != nil {
		conn.Close()
		t.Fatal("wrong producer token was accepted")
	}
	if err == nil || wsResponse == nil || wsResponse.StatusCode != http.StatusUnauthorized {
		t.Fatalf("wrong producer result err=%v status=%v", err, responseStatus(wsResponse))
	}
	wsResponse.Body.Close()

	conn, wsResponse, err = fixture.dialer.Dial(fixture.wsURL("/api/v1/publish"), http.Header{
		"Authorization": []string{"Bearer " + testProducerToken, "Bearer " + testProducerToken},
		"X-Radar-Room":  []string{testRoom},
	})
	if conn != nil {
		conn.Close()
		t.Fatal("duplicate Authorization headers were accepted")
	}
	if err == nil || wsResponse == nil || wsResponse.StatusCode != http.StatusUnauthorized {
		t.Fatalf("duplicate auth result err=%v status=%v", err, responseStatus(wsResponse))
	}
	wsResponse.Body.Close()

	conn, wsResponse, err = fixture.dialer.Dial(fixture.wsURL("/api/v1/stream"), http.Header{"Origin": []string{testOrigin}})
	if conn != nil {
		conn.Close()
		t.Fatal("viewer without a session was accepted")
	}
	if err == nil || wsResponse == nil || wsResponse.StatusCode != http.StatusUnauthorized {
		t.Fatalf("anonymous viewer result err=%v status=%v", err, responseStatus(wsResponse))
	}
	wsResponse.Body.Close()
}

func TestLoginRateLimitAppliesPerIP(t *testing.T) {
	fixture := newRelayFixture(t, func(cfg *config.Config) {
		cfg.LoginAttemptsPerMinute = 1
		cfg.LoginBurst = 1
	})
	for attempt, token := range []string{"wrong-invite-token-with-32-characters", testInviteToken} {
		payload, _ := json.Marshal(map[string]string{"room": testRoom, "inviteToken": token})
		request, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
		request.Header.Set("Origin", testOrigin)
		request.Header.Set("Content-Type", "application/json")
		response, err := fixture.http.Client().Do(request)
		if err != nil {
			t.Fatal(err)
		}
		response.Body.Close()
		want := http.StatusUnauthorized
		if attempt == 1 {
			want = http.StatusTooManyRequests
		}
		if response.StatusCode != want {
			t.Fatalf("attempt %d got %d, want %d", attempt, response.StatusCode, want)
		}
	}
}

func TestProducerViewerEndToEndAndSingleProducer(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)
	viewer := fixture.dialViewer(t, cookie)
	producer := fixture.dialProducer(t)
	snapshot := snapshotAt(time.Now(), 7)

	header := http.Header{
		"Authorization": []string{"Bearer " + testProducerToken},
		"X-Radar-Room":  []string{testRoom},
	}
	second, response, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/publish"), header)
	if second != nil {
		_ = second.Close()
		t.Fatal("second producer was accepted")
	}
	if err == nil || response == nil || response.StatusCode != http.StatusConflict {
		t.Fatalf("second producer result err=%v status=%v, want 409", err, responseStatus(response))
	}
	response.Body.Close()

	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshot)); err != nil {
		t.Fatal(err)
	}
	_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
	messageType, payload, err := viewer.ReadMessage()
	if err != nil {
		t.Fatal(err)
	}
	if messageType != websocket.TextMessage || string(payload) != snapshot {
		t.Fatalf("viewer got type=%d payload=%s", messageType, payload)
	}

	lateCookie := fixture.createSession(t)
	lateViewer := fixture.dialViewer(t, lateCookie)
	_ = lateViewer.SetReadDeadline(time.Now().Add(time.Second))
	_, latest, err := lateViewer.ReadMessage()
	if err != nil || string(latest) != snapshot {
		t.Fatalf("late viewer did not get latest absolute frame: %q, %v", latest, err)
	}

	thirdCookie := fixture.createSession(t)
	third, capacityResponse, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/stream"), http.Header{
		"Origin": []string{testOrigin}, "Cookie": []string{thirdCookie.String()},
	})
	if third != nil {
		_ = third.Close()
		t.Fatal("viewer capacity was not enforced")
	}
	if err == nil || capacityResponse == nil || capacityResponse.StatusCode != http.StatusServiceUnavailable {
		t.Fatalf("capacity result err=%v status=%v", err, responseStatus(capacityResponse))
	}
	capacityResponse.Body.Close()
}

func TestRoomsAreIsolated(t *testing.T) {
	const otherProducerToken = "other-producer-token-with-32-characters"
	fixture := newRelayFixture(t, func(cfg *config.Config) {
		cfg.Rooms = append(cfg.Rooms, config.RoomConfig{
			ID:                  "other_room",
			ProducerTokenSHA256: auth.HexSum(otherProducerToken),
			InviteTokenSHA256:   []string{auth.HexSum("other-invite-token-with-32-characters")},
			MaxViewers:          2,
		})
	})
	viewer := fixture.dialViewer(t, fixture.createSession(t))
	producer, response, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/publish"), http.Header{
		"Authorization": []string{"Bearer " + otherProducerToken},
		"X-Radar-Room":  []string{"other_room"},
	})
	if err != nil {
		if response != nil {
			response.Body.Close()
		}
		t.Fatal(err)
	}
	defer producer.Close()
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshotAt(time.Now(), 1))); err != nil {
		t.Fatal(err)
	}
	_ = viewer.SetReadDeadline(time.Now().Add(150 * time.Millisecond))
	if _, _, err := viewer.ReadMessage(); err == nil {
		t.Fatal("viewer received a snapshot published to another room")
	}
}

func TestViewerRejectsOriginAndApplicationData(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)
	conn, response, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/stream"), http.Header{
		"Origin": []string{"https://evil.example"}, "Cookie": []string{cookie.String()},
	})
	if conn != nil {
		_ = conn.Close()
		t.Fatal("cross-origin viewer was accepted")
	}
	if err == nil || response == nil || response.StatusCode != http.StatusForbidden {
		t.Fatalf("cross-origin result err=%v status=%v", err, responseStatus(response))
	}
	response.Body.Close()

	viewer := fixture.dialViewer(t, cookie)
	if err := viewer.WriteMessage(websocket.TextMessage, []byte("x")); err != nil {
		t.Fatal(err)
	}
	_, _, err = viewer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != websocket.ClosePolicyViolation {
		t.Fatalf("viewer application data close=%v, want 1008", err)
	}
}

func TestActiveViewerClosesWhenSessionExpires(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)
	hash := auth.Sum(cookie.Value)
	fixture.server.sessionsMu.Lock()
	current := fixture.server.sessions[hash]
	current.mu.Lock()
	current.expiresAt = time.Now().Add(100 * time.Millisecond)
	current.mu.Unlock()
	fixture.server.sessionsMu.Unlock()

	viewer := fixture.dialViewer(t, cookie)
	_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err := viewer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != 4401 {
		t.Fatalf("expired session close=%v, want 4401", err)
	}
}

func TestProducerRejectsInvalidSnapshotAndExcessRate(t *testing.T) {
	fixture := newRelayFixture(t, func(cfg *config.Config) {
		cfg.MaxPublishHz = 1
		cfg.PublishBurst = 1
	})
	producer := fixture.dialProducer(t)
	snapshot := snapshotAt(time.Now(), 1)
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshot)); err != nil {
		t.Fatal(err)
	}
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshot)); err != nil {
		t.Fatal(err)
	}
	_ = producer.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err := producer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != websocket.ClosePolicyViolation {
		t.Fatalf("rate close=%v, want 1008", err)
	}

	// The producer slot is released after the rate-limited connection exits.
	var replacement *websocket.Conn
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		candidate, _, dialErr := fixture.dialer.Dial(fixture.wsURL("/api/v1/publish"), http.Header{
			"Authorization": []string{"Bearer " + testProducerToken}, "X-Radar-Room": []string{testRoom},
		})
		if dialErr == nil {
			replacement = candidate
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if replacement == nil {
		t.Fatal("producer slot was not released")
	}
	defer replacement.Close()
	if err := replacement.WriteMessage(websocket.TextMessage, []byte(`{"v":1,"type":"hello"}`)); err != nil {
		t.Fatal(err)
	}
	_ = replacement.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err = replacement.ReadMessage()
	if !errorsAs(err, &closeErr) || closeErr.Code != websocket.CloseInvalidFramePayloadData {
		t.Fatalf("invalid snapshot close=%v, want 1007", err)
	}
}

func TestProducerFrameSizeLimit(t *testing.T) {
	fixture := newRelayFixture(t, func(cfg *config.Config) {
		cfg.MaxSnapshotBytes = 4096
	})
	producer := fixture.dialProducer(t)
	if err := producer.WriteMessage(websocket.TextMessage, []byte(strings.Repeat("x", 4097))); err != nil {
		t.Fatal(err)
	}
	_ = producer.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err := producer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != websocket.CloseMessageTooBig {
		t.Fatalf("oversized frame close=%v, want 1009", err)
	}
}

func TestExpiredLatestFrameIsNotReplayed(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)
	producer := fixture.dialProducer(t)
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshotAt(time.Now(), 1))); err != nil {
		t.Fatal(err)
	}
	time.Sleep(350 * time.Millisecond)
	viewer := fixture.dialViewer(t, cookie)
	_ = viewer.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	if _, _, err := viewer.ReadMessage(); err == nil {
		t.Fatal("expired latest frame was replayed")
	}
}

func TestSessionViewerLimitAndImmediateLogoutRevocation(t *testing.T) {
	fixture := newRelayFixture(t, func(cfg *config.Config) {
		cfg.MaxViewersPerSession = 1
	})
	cookie := fixture.createSession(t)
	viewer := fixture.dialViewer(t, cookie)

	second, response, err := fixture.dialer.Dial(fixture.wsURL("/api/v1/stream"), http.Header{
		"Origin": []string{testOrigin}, "Cookie": []string{cookie.String()},
	})
	if second != nil {
		_ = second.Close()
		t.Fatal("one session exceeded its viewer limit")
	}
	if err == nil || response == nil || response.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("per-session capacity result err=%v status=%v", err, responseStatus(response))
	}
	response.Body.Close()

	request, _ := http.NewRequest(http.MethodDelete, fixture.http.URL+"/api/v1/session", nil)
	request.Header.Set("Origin", testOrigin)
	request.AddCookie(cookie)
	deleteResponse, err := fixture.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	deleteResponse.Body.Close()
	if deleteResponse.StatusCode != http.StatusNoContent {
		t.Fatalf("logout returned %d", deleteResponse.StatusCode)
	}
	_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err = viewer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != 4401 {
		t.Fatalf("revoked session close=%v, want 4401", err)
	}
}

func TestReplacingCookieRevokesItsActiveViewer(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	oldCookie := fixture.createSession(t)
	viewer := fixture.dialViewer(t, oldCookie)
	payload, _ := json.Marshal(map[string]string{"room": testRoom, "inviteToken": testInviteToken})
	request, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
	request.Header.Set("Origin", testOrigin)
	request.Header.Set("Content-Type", "application/json")
	request.AddCookie(oldCookie)
	response, err := fixture.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("replacement login returned %d", response.StatusCode)
	}
	_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err = viewer.ReadMessage()
	var closeErr *websocket.CloseError
	if !errorsAs(err, &closeErr) || closeErr.Code != 4401 {
		t.Fatalf("replaced session close=%v, want 4401", err)
	}
}

func TestProducerRejectsStaleAndFutureSnapshots(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	for _, test := range []struct {
		name       string
		capturedAt time.Time
	}{
		{"stale", time.Now().Add(-20 * time.Second)},
		{"future", time.Now().Add(40 * time.Second)},
	} {
		t.Run(test.name, func(t *testing.T) {
			producer := fixture.dialProducer(t)
			if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshotAt(test.capturedAt, 1))); err != nil {
				t.Fatal(err)
			}
			_ = producer.SetReadDeadline(time.Now().Add(time.Second))
			_, _, err := producer.ReadMessage()
			var closeErr *websocket.CloseError
			if !errorsAs(err, &closeErr) || closeErr.Code != websocket.ClosePolicyViolation {
				t.Fatalf("temporal policy close=%v, want 1008", err)
			}
			_ = producer.Close()
			// Let the handler release the single-producer reservation.
			time.Sleep(20 * time.Millisecond)
		})
	}
}

func TestServerCloseGracefullyClosesWebSockets(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)
	viewer := fixture.dialViewer(t, cookie)
	producer := fixture.dialProducer(t)
	fixture.server.Close()
	for name, conn := range map[string]*websocket.Conn{"viewer": viewer, "producer": producer} {
		_ = conn.SetReadDeadline(time.Now().Add(time.Second))
		_, _, err := conn.ReadMessage()
		var closeErr *websocket.CloseError
		if !errorsAs(err, &closeErr) || closeErr.Code != websocket.CloseGoingAway {
			t.Fatalf("%s shutdown close=%v, want 1001", name, err)
		}
	}
}

func TestViewerCompressionNegotiationAndLegacyFallback(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	cookie := fixture.createSession(t)

	compressedDialer := *fixture.dialer
	compressedDialer.EnableCompression = true
	header := http.Header{"Origin": []string{testOrigin}, "Cookie": []string{cookie.String()}}
	compressed, response, err := compressedDialer.Dial(fixture.wsURL("/api/v1/stream"), header)
	if err != nil {
		if response != nil {
			response.Body.Close()
		}
		t.Fatal(err)
	}
	defer compressed.Close()
	if extension := response.Header.Get("Sec-WebSocket-Extensions"); !strings.Contains(extension, "permessage-deflate") ||
		!strings.Contains(extension, "server_no_context_takeover") {
		t.Fatalf("compression was not safely negotiated: %q", extension)
	}

	// The fixture's default dialer does not offer compression. Its successful
	// connection proves old clients retain the uncompressed fallback path.
	legacy := fixture.dialViewer(t, fixture.createSession(t))
	producer := fixture.dialProducer(t)
	snapshot := snapshotAt(time.Now(), 88)
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshot)); err != nil {
		t.Fatal(err)
	}
	for name, viewer := range map[string]*websocket.Conn{"compressed": compressed, "legacy": legacy} {
		_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
		_, payload, readErr := viewer.ReadMessage()
		if readErr != nil || string(payload) != snapshot {
			t.Fatalf("%s viewer received %q, %v", name, payload, readErr)
		}
	}
}

func TestMetricsAreAggregateAndTrackTraffic(t *testing.T) {
	fixture := newRelayFixture(t, func(cfg *config.Config) { cfg.EnableMetrics = true })
	cookie := fixture.createSession(t)
	viewer := fixture.dialViewer(t, cookie)
	producer := fixture.dialProducer(t)
	snapshot := snapshotAt(time.Now(), 99)
	if err := producer.WriteMessage(websocket.TextMessage, []byte(snapshot)); err != nil {
		t.Fatal(err)
	}
	_ = viewer.SetReadDeadline(time.Now().Add(time.Second))
	if _, _, err := viewer.ReadMessage(); err != nil {
		t.Fatal(err)
	}

	response, err := fixture.http.Client().Get(fixture.http.URL + "/metrics")
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	payload, _ := io.ReadAll(response.Body)
	if response.StatusCode != http.StatusOK || !strings.HasPrefix(response.Header.Get("Content-Type"), "text/plain; version=0.0.4") {
		t.Fatalf("metrics response status=%d content-type=%q", response.StatusCode, response.Header.Get("Content-Type"))
	}
	for _, expected := range []string{
		"radar_relay_sessions_active 1",
		"radar_relay_producers_active 1",
		"radar_relay_viewers_active 1",
		"radar_relay_snapshots_published_total 1",
		"radar_relay_viewer_frames_sent_total 1",
	} {
		if !strings.Contains(string(payload), expected) {
			t.Fatalf("metrics do not contain %q:\n%s", expected, payload)
		}
	}
	for _, sensitive := range []string{testRoom, testProducerToken, testInviteToken, "127.0.0.1"} {
		if strings.Contains(string(payload), sensitive) {
			t.Fatalf("metrics exposed sensitive or identifying value %q", sensitive)
		}
	}
	if response.Header.Get("Cache-Control") != "no-store" {
		t.Fatal("metrics response is cacheable")
	}
}

func TestMetricsAreDisabledByDefault(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	response, err := fixture.http.Client().Get(fixture.http.URL + "/metrics")
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusNotFound {
		t.Fatalf("disabled metrics returned %d, want 404", response.StatusCode)
	}
}

func TestNotReadyRejectsNewSession(t *testing.T) {
	fixture := newRelayFixture(t, nil)
	fixture.server.SetReady(false)
	payload := []byte(`{"room":"test_room","inviteToken":"invite-token-with-at-least-32-characters"}`)
	request, _ := http.NewRequest(http.MethodPost, fixture.http.URL+"/api/v1/session", bytes.NewReader(payload))
	request.Header.Set("Origin", testOrigin)
	request.Header.Set("Content-Type", "application/json")
	response, err := fixture.http.Client().Do(request)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusServiceUnavailable {
		t.Fatalf("not-ready login got %d, want 503", response.StatusCode)
	}
}

func TestStaticCachingAndMissingAssetBehavior(t *testing.T) {
	root := t.TempDir()
	for _, directory := range []string{"assets", "maps/de_test"} {
		if err := os.MkdirAll(filepath.Join(root, directory), 0755); err != nil {
			t.Fatal(err)
		}
	}
	for name, contents := range map[string]string{
		"index.html":                   "<html>radar shell</html>",
		"assets/index-Ab12_cd9.js":     "console.log('hashed')",
		"assets/unversioned-helper.js": "console.log('plain')",
		"maps/de_test/radar.png":       "map",
	} {
		if err := os.WriteFile(filepath.Join(root, name), []byte(contents), 0644); err != nil {
			t.Fatal(err)
		}
	}
	fixture := newRelayFixture(t, func(cfg *config.Config) { cfg.StaticDir = root })

	assertResponse := func(path string, status int, body, cacheControl string) {
		t.Helper()
		response, err := fixture.http.Client().Get(fixture.http.URL + path)
		if err != nil {
			t.Fatal(err)
		}
		defer response.Body.Close()
		payload, _ := io.ReadAll(response.Body)
		if response.StatusCode != status || (body != "" && !strings.Contains(string(payload), body)) {
			t.Fatalf("GET %s returned status=%d body=%q", path, response.StatusCode, payload)
		}
		if response.Header.Get("Cache-Control") != cacheControl {
			t.Fatalf("GET %s cache-control=%q, want %q", path, response.Header.Get("Cache-Control"), cacheControl)
		}
	}

	assertResponse("/assets/index-Ab12_cd9.js", http.StatusOK, "hashed", "public, max-age=31536000, immutable")
	assertResponse("/assets/missing-Ab12_cd9.js", http.StatusNotFound, "404", "no-store")
	assertResponse("/maps/de_test/missing.png", http.StatusNotFound, "404", "no-store")
	assertResponse("/missing.css", http.StatusNotFound, "404", "no-store")
	assertResponse("/dashboard", http.StatusOK, "radar shell", "no-store")
}

func responseStatus(response *http.Response) any {
	if response == nil {
		return nil
	}
	return response.StatusCode
}

// Kept as a helper so this test file remains easy to audit for every expected
// WebSocket close code.
func errorsAs(err error, target any) bool {
	return errors.As(err, target)
}
