package relay

import (
	"compress/flate"
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/auth"
	"github.com/tiansongyu/cs2_cheat/radar-relay/internal/config"
)

const sessionCookieName = "__Host-radar_session"

type Server struct {
	config       config.Config
	logger       *slog.Logger
	rooms        map[string]*room
	staticRoot   string
	dummyHash    [sha256.Size]byte
	sessionsMu   sync.Mutex
	sessions     map[[sha256.Size]byte]*session
	loginLimiter *ipLimiter
	ipResolver   clientIPResolver
	ready        atomic.Bool
	closeOnce    sync.Once
	stopJanitor  context.CancelFunc
	janitorDone  chan struct{}
	metrics      relayMetrics
	handler      http.Handler
}

func New(cfg config.Config, logger *slog.Logger) (*Server, error) {
	if err := cfg.Validate(); err != nil {
		return nil, err
	}
	staticRoot := ""
	if cfg.StaticDir != "" {
		root, err := filepath.Abs(cfg.StaticDir)
		if err != nil {
			return nil, fmt.Errorf("staticDir: %w", err)
		}
		root, err = filepath.EvalSymlinks(root)
		if err != nil {
			return nil, fmt.Errorf("staticDir: %w", err)
		}
		info, err := os.Stat(root)
		if err != nil {
			return nil, fmt.Errorf("staticDir: %w", err)
		}
		if !info.IsDir() {
			return nil, errors.New("staticDir must name a directory")
		}
		staticRoot = root
	}
	if logger == nil {
		logger = slog.New(slog.NewTextHandler(io.Discard, nil))
	}
	server := &Server{
		config:       cfg,
		logger:       logger,
		rooms:        make(map[string]*room, len(cfg.Rooms)),
		staticRoot:   staticRoot,
		dummyHash:    auth.Sum("radar-relay-invalid-room-dummy-token"),
		sessions:     make(map[[sha256.Size]byte]*session),
		loginLimiter: newIPLimiter(cfg.LoginAttemptsPerMinute, cfg.LoginBurst, cfg.MaxTrackedIPs),
		ipResolver:   newClientIPResolver(cfg.TrustedProxyCIDRs),
		janitorDone:  make(chan struct{}),
	}
	for _, roomConfig := range cfg.Rooms {
		producerHash, _ := auth.ParseHexSum(roomConfig.ProducerTokenSHA256)
		inviteHashes := make([][sha256.Size]byte, 0, len(roomConfig.InviteTokenSHA256))
		for _, encoded := range roomConfig.InviteTokenSHA256 {
			hash, _ := auth.ParseHexSum(encoded)
			inviteHashes = append(inviteHashes, hash)
		}
		server.rooms[roomConfig.ID] = &room{
			id:           roomConfig.ID,
			producerHash: producerHash,
			inviteHashes: inviteHashes,
			maxViewers:   roomConfig.MaxViewers,
			viewers:      make(map[*viewer]struct{}),
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", server.handleHealth)
	mux.HandleFunc("/readyz", server.handleReady)
	mux.HandleFunc("/metrics", server.handleMetrics)
	mux.HandleFunc("/api/v1/session", server.handleSession)
	mux.HandleFunc("/api/v1/publish", server.handleProducer)
	mux.HandleFunc("/api/v1/stream", server.handleViewer)
	mux.HandleFunc("/", server.handleStatic)
	server.handler = server.securityHeaders(mux)
	server.ready.Store(true)

	janitorContext, cancel := context.WithCancel(context.Background())
	server.stopJanitor = cancel
	go func() {
		defer close(server.janitorDone)
		server.runJanitor(janitorContext)
	}()
	return server, nil
}

func (s *Server) Handler() http.Handler {
	return s.handler
}

func (s *Server) SetReady(ready bool) {
	s.ready.Store(ready)
}

func (s *Server) Close() {
	s.closeOnce.Do(func() {
		s.ready.Store(false)
		if s.stopJanitor != nil {
			s.stopJanitor()
		}
		if s.janitorDone != nil {
			<-s.janitorDone
		}
		s.sessionsMu.Lock()
		activeSessions := make([]*session, 0, len(s.sessions))
		for _, currentSession := range s.sessions {
			activeSessions = append(activeSessions, currentSession)
		}
		clear(s.sessions)
		s.sessionsMu.Unlock()
		deadline := time.Now().Add(time.Second)
		for _, currentSession := range activeSessions {
			currentSession.revokeAt(websocket.CloseGoingAway, "server shutdown", deadline)
		}
		for _, currentRoom := range s.rooms {
			currentRoom.closeConnections(deadline)
		}
	})
}

func (s *Server) runJanitor(ctx context.Context) {
	interval := s.config.SnapshotTTL() / 2
	if interval < 250*time.Millisecond {
		interval = 250 * time.Millisecond
	}
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-ticker.C:
			for _, currentRoom := range s.rooms {
				currentRoom.expire(now, s.config.SnapshotTTL())
			}
			s.pruneSessions(now)
		}
	}
}

func (s *Server) securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		header := writer.Header()
		header.Set("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; form-action 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'")
		header.Set("Cross-Origin-Opener-Policy", "same-origin")
		header.Set("Cross-Origin-Resource-Policy", "same-origin")
		header.Set("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
		header.Set("Referrer-Policy", "no-referrer")
		header.Set("X-Content-Type-Options", "nosniff")
		header.Set("X-Frame-Options", "DENY")
		header.Set("Strict-Transport-Security", "max-age=31536000")
		if strings.HasPrefix(request.URL.Path, "/api/") || request.URL.Path == "/healthz" || request.URL.Path == "/readyz" || request.URL.Path == "/metrics" {
			header.Set("Cache-Control", "no-store")
		}
		if strings.HasPrefix(request.URL.Path, "/api/") && request.URL.RawQuery != "" {
			writeError(writer, http.StatusBadRequest, "query_not_allowed")
			return
		}
		next.ServeHTTP(writer, request)
	})
}

func (s *Server) handleHealth(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		methodNotAllowed(writer, http.MethodGet)
		return
	}
	writeJSON(writer, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *Server) handleReady(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		methodNotAllowed(writer, http.MethodGet)
		return
	}
	if !s.ready.Load() {
		writeJSON(writer, http.StatusServiceUnavailable, map[string]string{"status": "not_ready"})
		return
	}
	writeJSON(writer, http.StatusOK, map[string]string{"status": "ready"})
}

func (s *Server) handleSession(writer http.ResponseWriter, request *http.Request) {
	switch request.Method {
	case http.MethodGet:
		s.handleGetSession(writer, request)
	case http.MethodPost:
		if !s.validOrigin(request) {
			writeError(writer, http.StatusForbidden, "forbidden")
			return
		}
		s.handleCreateSession(writer, request)
	case http.MethodDelete:
		if !s.validOrigin(request) {
			writeError(writer, http.StatusForbidden, "forbidden")
			return
		}
		s.handleDeleteSession(writer, request)
	default:
		writer.Header().Set("Allow", "GET, POST, DELETE")
		writeError(writer, http.StatusMethodNotAllowed, "method_not_allowed")
	}
}

func (s *Server) handleGetSession(writer http.ResponseWriter, request *http.Request) {
	current, ok := s.authenticateSession(request, time.Now())
	if !ok {
		writeJSON(writer, http.StatusUnauthorized, map[string]bool{"authenticated": false})
		return
	}
	writeJSON(writer, http.StatusOK, struct {
		Authenticated bool   `json:"authenticated"`
		Room          string `json:"room"`
		ExpiresAtMs   int64  `json:"expiresAtMs"`
	}{true, current.room.id, current.expiresAt.UnixMilli()})
}

func (s *Server) handleCreateSession(writer http.ResponseWriter, request *http.Request) {
	if !s.ready.Load() {
		writeError(writer, http.StatusServiceUnavailable, "not_ready")
		return
	}
	now := time.Now()
	clientIP := s.ipResolver.resolve(request)
	if !s.loginLimiter.allow(clientIP, now) {
		s.metrics.loginRateLimited.Add(1)
		writer.Header().Set("Retry-After", "60")
		writeError(writer, http.StatusTooManyRequests, "rate_limited")
		return
	}
	mediaType, _, err := mime.ParseMediaType(request.Header.Get("Content-Type"))
	if err != nil || mediaType != "application/json" {
		writeError(writer, http.StatusUnsupportedMediaType, "application_json_required")
		return
	}
	request.Body = http.MaxBytesReader(writer, request.Body, 8192)
	decoder := json.NewDecoder(request.Body)
	decoder.DisallowUnknownFields()
	var body struct {
		Room        string `json:"room"`
		InviteToken string `json:"inviteToken"`
	}
	if err := decoder.Decode(&body); err != nil || !atJSONEOF(decoder) {
		writeError(writer, http.StatusBadRequest, "invalid_request")
		return
	}
	currentRoom, exists := s.rooms[body.Room]
	inviteHashes := [][sha256.Size]byte{s.dummyHash}
	if exists {
		inviteHashes = currentRoom.inviteHashes
	}
	tokenValid := auth.ValidPresentedToken(body.InviteToken)
	tokenMatches := auth.MatchesAny(body.InviteToken, inviteHashes)
	if !exists || !tokenValid || !tokenMatches {
		s.metrics.loginAuthFailures.Add(1)
		writeError(writer, http.StatusUnauthorized, "invalid_credentials")
		return
	}

	token, err := auth.GenerateToken()
	if err != nil {
		s.logger.Error("session entropy source failed", "error", err)
		writeError(writer, http.StatusInternalServerError, "server_error")
		return
	}
	expiresAt := now.Add(s.config.SessionTTL())
	hash := auth.Sum(token)
	s.sessionsMu.Lock()
	expiredSessions := s.pruneSessionsLocked(now)
	var replaced *session
	if oldCookie, cookieErr := request.Cookie(sessionCookieName); cookieErr == nil {
		oldHash := auth.Sum(oldCookie.Value)
		replaced = s.sessions[oldHash]
		delete(s.sessions, oldHash)
	}
	if len(s.sessions) >= s.config.MaxSessions {
		s.sessionsMu.Unlock()
		revokeSessions(expiredSessions, 4401, "session expired")
		if replaced != nil {
			replaced.revoke(4401, "session replaced")
		}
		writeError(writer, http.StatusServiceUnavailable, "session_capacity")
		return
	}
	s.sessions[hash] = newSession(currentRoom, expiresAt)
	s.metrics.sessionsCreated.Add(1)
	s.sessionsMu.Unlock()
	revokeSessions(expiredSessions, 4401, "session expired")
	if replaced != nil {
		replaced.revoke(4401, "session replaced")
	}

	http.SetCookie(writer, &http.Cookie{
		Name:     sessionCookieName,
		Value:    token,
		Path:     "/",
		Expires:  expiresAt,
		MaxAge:   int(s.config.SessionTTL().Seconds()),
		Secure:   true,
		HttpOnly: true,
		SameSite: http.SameSiteStrictMode,
	})
	writeJSON(writer, http.StatusOK, struct {
		Authenticated bool   `json:"authenticated"`
		Room          string `json:"room"`
		ExpiresAtMs   int64  `json:"expiresAtMs"`
	}{true, currentRoom.id, expiresAt.UnixMilli()})
}

func (s *Server) handleDeleteSession(writer http.ResponseWriter, request *http.Request) {
	var revoked *session
	if cookie, err := request.Cookie(sessionCookieName); err == nil {
		s.sessionsMu.Lock()
		hash := auth.Sum(cookie.Value)
		revoked = s.sessions[hash]
		delete(s.sessions, hash)
		s.sessionsMu.Unlock()
	}
	if revoked != nil {
		revoked.revoke(4401, "session revoked")
	}
	http.SetCookie(writer, &http.Cookie{
		Name:     sessionCookieName,
		Value:    "",
		Path:     "/",
		MaxAge:   -1,
		Expires:  time.Unix(1, 0),
		Secure:   true,
		HttpOnly: true,
		SameSite: http.SameSiteStrictMode,
	})
	writer.WriteHeader(http.StatusNoContent)
}

func (s *Server) authenticateSession(request *http.Request, now time.Time) (*session, bool) {
	cookie, err := request.Cookie(sessionCookieName)
	if err != nil || !auth.ValidPresentedToken(cookie.Value) {
		return nil, false
	}
	hash := auth.Sum(cookie.Value)
	s.sessionsMu.Lock()
	current, exists := s.sessions[hash]
	if !exists {
		s.sessionsMu.Unlock()
		return nil, false
	}
	if !current.valid(now) {
		delete(s.sessions, hash)
		s.sessionsMu.Unlock()
		current.revoke(4401, "session expired")
		return nil, false
	}
	s.sessionsMu.Unlock()
	return current, true
}

func (s *Server) pruneSessions(now time.Time) {
	s.sessionsMu.Lock()
	expired := s.pruneSessionsLocked(now)
	s.sessionsMu.Unlock()
	revokeSessions(expired, 4401, "session expired")
}

func (s *Server) pruneSessionsLocked(now time.Time) []*session {
	var expired []*session
	for hash, current := range s.sessions {
		if !current.valid(now) {
			delete(s.sessions, hash)
			expired = append(expired, current)
		}
	}
	return expired
}

func revokeSessions(sessions []*session, code int, reason string) {
	deadline := time.Now().Add(time.Second)
	for _, current := range sessions {
		current.revokeAt(code, reason, deadline)
	}
}

func (s *Server) handleProducer(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		methodNotAllowed(writer, http.MethodGet)
		return
	}
	if !s.ready.Load() {
		writeError(writer, http.StatusServiceUnavailable, "not_ready")
		return
	}
	currentRoom, ok := s.authenticateProducer(request)
	if !ok {
		s.metrics.producerAuthFailures.Add(1)
		writer.Header().Set("WWW-Authenticate", "Bearer")
		writeError(writer, http.StatusUnauthorized, "invalid_credentials")
		return
	}
	generation, err := currentRoom.claimProducer()
	if err != nil {
		s.metrics.producerConflicts.Add(1)
		writeError(writer, http.StatusConflict, "producer_already_connected")
		return
	}
	attached := false
	defer func() {
		if !attached {
			currentRoom.releaseProducer(generation)
		}
	}()

	upgrader := websocket.Upgrader{
		HandshakeTimeout: 5 * time.Second,
		ReadBufferSize:   4096,
		WriteBufferSize:  1024,
		CheckOrigin:      func(*http.Request) bool { return true },
	}
	conn, err := upgrader.Upgrade(writer, request, nil)
	if err != nil {
		return
	}
	if !currentRoom.attachProducer(generation, conn) {
		_ = conn.Close()
		return
	}
	attached = true
	s.metrics.producerConnections.Add(1)
	defer currentRoom.releaseProducer(generation)
	defer conn.Close()

	conn.SetReadLimit(s.config.MaxSnapshotBytes)
	_ = conn.SetReadDeadline(time.Now().Add(s.config.ProducerIdleTimeout()))
	limiter := newFrameLimiter(s.config.MaxPublishHz, s.config.PublishBurst, time.Now())
	for {
		messageType, payload, readErr := conn.ReadMessage()
		if readErr != nil {
			if errors.Is(readErr, websocket.ErrReadLimit) {
				s.metrics.snapshotsRejected.Add(1)
			}
			return
		}
		now := time.Now()
		if messageType != websocket.TextMessage {
			s.metrics.snapshotsRejected.Add(1)
			closeWebSocket(conn, websocket.CloseUnsupportedData, "text snapshots only")
			return
		}
		if !limiter.allow(now) {
			s.metrics.snapshotsRejected.Add(1)
			closeWebSocket(conn, websocket.ClosePolicyViolation, "publish rate exceeded")
			return
		}
		capturedAt, validationErr := validateSnapshot(payload)
		if validationErr != nil {
			s.metrics.snapshotsRejected.Add(1)
			closeWebSocket(conn, websocket.CloseInvalidFramePayloadData, "invalid snapshot")
			return
		}
		if capturedAt.Before(now.Add(-s.config.MaxSnapshotAge())) || capturedAt.After(now.Add(s.config.MaxFutureSkew())) {
			s.metrics.snapshotsRejected.Add(1)
			closeWebSocket(conn, websocket.ClosePolicyViolation, "snapshot capture time outside allowed window")
			return
		}
		_ = conn.SetReadDeadline(now.Add(s.config.ProducerIdleTimeout()))
		dropped, publishErr := currentRoom.publish(payload, now)
		if publishErr != nil {
			s.metrics.snapshotsRejected.Add(1)
			closeWebSocket(conn, websocket.CloseInternalServerErr, "snapshot preparation failed")
			return
		}
		s.metrics.snapshotsPublished.Add(1)
		s.metrics.snapshotBytes.Add(uint64(len(payload)))
		s.metrics.viewerFramesDropped.Add(uint64(dropped))
	}
}

func (s *Server) authenticateProducer(request *http.Request) (*room, bool) {
	roomHeaders := request.Header.Values("X-Radar-Room")
	authorizationHeaders := request.Header.Values("Authorization")
	if len(roomHeaders) != 1 || len(authorizationHeaders) != 1 {
		return nil, false
	}
	currentRoom, exists := s.rooms[roomHeaders[0]]
	authorization := authorizationHeaders[0]
	if !strings.HasPrefix(authorization, "Bearer ") || strings.Count(authorization, " ") != 1 {
		return nil, false
	}
	token := strings.TrimPrefix(authorization, "Bearer ")
	expected := s.dummyHash
	if exists {
		expected = currentRoom.producerHash
	}
	if !auth.ValidPresentedToken(token) || !auth.Matches(token, expected) || !exists {
		return nil, false
	}
	return currentRoom, true
}

func (s *Server) handleViewer(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet {
		methodNotAllowed(writer, http.MethodGet)
		return
	}
	if !s.ready.Load() {
		writeError(writer, http.StatusServiceUnavailable, "not_ready")
		return
	}
	if !s.validOrigin(request) {
		writeError(writer, http.StatusForbidden, "forbidden")
		return
	}
	currentSession, ok := s.authenticateSession(request, time.Now())
	if !ok {
		s.metrics.viewerAuthFailures.Add(1)
		writeError(writer, http.StatusUnauthorized, "authentication_required")
		return
	}
	sessionReservation, ok := currentSession.reserveViewer(time.Now(), s.config.MaxViewersPerSession)
	if !ok {
		s.metrics.viewerCapacityRejected.Add(1)
		writeError(writer, http.StatusTooManyRequests, "session_viewer_capacity")
		return
	}
	defer currentSession.releaseViewer(sessionReservation)

	currentViewer := newViewer()
	latest, err := currentSession.room.addViewer(currentViewer, time.Now(), s.config.SnapshotTTL())
	if err != nil {
		s.metrics.viewerCapacityRejected.Add(1)
		writeError(writer, http.StatusServiceUnavailable, "viewer_capacity")
		return
	}
	defer currentSession.room.removeViewer(currentViewer)
	upgrader := websocket.Upgrader{
		HandshakeTimeout:  5 * time.Second,
		ReadBufferSize:    1024,
		WriteBufferSize:   16 * 1024,
		EnableCompression: true,
		CheckOrigin: func(request *http.Request) bool {
			return s.validOrigin(request)
		},
	}
	conn, err := upgrader.Upgrade(writer, request, nil)
	if err != nil {
		return
	}
	// BestSpeed keeps producer-to-viewer latency and CPU bounded. Gorilla uses
	// no-context-takeover, and clients which do not offer permessage-deflate
	// transparently continue on the uncompressed representation.
	if err := conn.SetCompressionLevel(flate.BestSpeed); err != nil {
		_ = conn.Close()
		return
	}
	if !currentSession.room.attachViewer(currentViewer, conn) {
		_ = conn.Close()
		return
	}
	if !currentSession.attachViewer(sessionReservation, conn, time.Now()) {
		closeWebSocket(conn, 4401, "session expired")
		_ = conn.Close()
		return
	}
	s.metrics.viewerConnections.Add(1)
	defer conn.Close()
	if latest.prepared != nil {
		currentViewer.offer(latest)
	}

	writerFailed := make(chan struct{}, 1)
	done := make(chan struct{})
	defer close(done)
	go s.writeViewer(conn, currentViewer, currentSession, writerFailed, done)
	conn.SetReadLimit(1)
	_ = conn.SetReadDeadline(viewerReadDeadline(time.Now(), currentSession.expiresAt))
	conn.SetPongHandler(func(string) error {
		return conn.SetReadDeadline(viewerReadDeadline(time.Now(), currentSession.expiresAt))
	})
	for {
		select {
		case <-writerFailed:
			return
		default:
		}
		_, _, readErr := conn.ReadMessage()
		if readErr != nil {
			return
		}
		closeWebSocket(conn, websocket.ClosePolicyViolation, "viewer is receive-only")
		return
	}
}

func (s *Server) writeViewer(conn *websocket.Conn, currentViewer *viewer, currentSession *session, failed chan<- struct{}, done <-chan struct{}) {
	ping := time.NewTicker(25 * time.Second)
	defer ping.Stop()
	expiry := time.NewTimer(time.Until(currentSession.expiresAt))
	defer expiry.Stop()
	for {
		select {
		case <-done:
			return
		case <-currentSession.done:
			return
		case <-expiry.C:
			currentSession.revoke(4401, "session expired")
			notifyFailure(failed)
			return
		case update := <-currentViewer.updates:
			now := time.Now()
			expiresAt := update.publishedAt.Add(s.config.SnapshotTTL())
			if !expiresAt.After(now) {
				continue
			}
			writeDeadline := now.Add(5 * time.Second)
			if expiresAt.Before(writeDeadline) {
				writeDeadline = expiresAt
			}
			_ = conn.SetWriteDeadline(writeDeadline)
			if err := conn.WritePreparedMessage(update.prepared); err != nil {
				s.metrics.websocketWriteErrors.Add(1)
				notifyFailure(failed)
				_ = conn.Close()
				return
			}
			s.metrics.viewerFramesSent.Add(1)
			s.metrics.viewerPayloadBytes.Add(uint64(update.payloadSize))
		case <-ping.C:
			if err := conn.WriteControl(websocket.PingMessage, nil, time.Now().Add(5*time.Second)); err != nil {
				s.metrics.websocketWriteErrors.Add(1)
				notifyFailure(failed)
				_ = conn.Close()
				return
			}
		}
	}
}

func viewerReadDeadline(now, expiresAt time.Time) time.Time {
	keepaliveDeadline := now.Add(70 * time.Second)
	// The expiry timer sends the authoritative 4401 close. A short read grace
	// prevents the socket deadline from racing that close frame, while pong
	// traffic still cannot extend the connection indefinitely past expiry.
	expiryGraceDeadline := expiresAt.Add(2 * time.Second)
	if expiryGraceDeadline.Before(keepaliveDeadline) {
		return expiryGraceDeadline
	}
	return keepaliveDeadline
}

func notifyFailure(channel chan<- struct{}) {
	select {
	case channel <- struct{}{}:
	default:
	}
}

func closeWebSocket(conn *websocket.Conn, code int, reason string) {
	_ = conn.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(code, reason), time.Now().Add(time.Second))
}

func (s *Server) validOrigin(request *http.Request) bool {
	origins := request.Header.Values("Origin")
	return len(origins) == 1 && origins[0] == s.config.PublicOrigin
}

func (s *Server) handleStatic(writer http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodGet && request.Method != http.MethodHead {
		methodNotAllowed(writer, "GET, HEAD")
		return
	}
	if s.staticRoot == "" {
		staticNotFound(writer, request)
		return
	}
	if strings.HasPrefix(request.URL.Path, "/api/") || hasHiddenPathSegment(request.URL.Path) {
		staticNotFound(writer, request)
		return
	}
	requested := filepath.Clean(filepath.FromSlash(strings.TrimPrefix(request.URL.Path, "/")))
	if requested == "." {
		requested = "index.html"
	}
	target, err := filepath.Abs(filepath.Join(s.staticRoot, requested))
	if err != nil || !pathWithinRoot(s.staticRoot, target) {
		staticNotFound(writer, request)
		return
	}
	info, statErr := os.Stat(target)
	if statErr != nil || info.IsDir() || !info.Mode().IsRegular() {
		if !shouldFallbackToIndex(requested) {
			staticNotFound(writer, request)
			return
		}
		target = filepath.Join(s.staticRoot, "index.html")
		if info, statErr = os.Stat(target); statErr != nil || !info.Mode().IsRegular() {
			staticNotFound(writer, request)
			return
		}
	}
	target, err = filepath.EvalSymlinks(target)
	if err != nil || !pathWithinRoot(s.staticRoot, target) {
		staticNotFound(writer, request)
		return
	}
	if filepath.Base(target) == "index.html" {
		writer.Header().Set("Cache-Control", "no-store")
	} else if isHashedAssetPath(requested) {
		writer.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
	http.ServeFile(writer, request, target)
}

func staticNotFound(writer http.ResponseWriter, request *http.Request) {
	// Error responses must not outlive a deployment which later restores the
	// referenced hashed asset.
	writer.Header().Set("Cache-Control", "no-store")
	http.NotFound(writer, request)
}

func shouldFallbackToIndex(requested string) bool {
	normalized := filepath.ToSlash(requested)
	first, _, _ := strings.Cut(normalized, "/")
	if first == "assets" || first == "maps" {
		return false
	}
	return filepath.Ext(normalized) == ""
}

func isHashedAssetPath(requested string) bool {
	normalized := filepath.ToSlash(requested)
	if !strings.HasPrefix(normalized, "assets/") {
		return false
	}
	base := filepath.Base(normalized)
	extension := filepath.Ext(base)
	stem := strings.TrimSuffix(base, extension)
	dash := strings.LastIndexByte(stem, '-')
	if extension == "" || dash < 0 || len(stem)-dash-1 < 8 {
		return false
	}
	for _, character := range stem[dash+1:] {
		if (character < 'a' || character > 'z') &&
			(character < 'A' || character > 'Z') &&
			(character < '0' || character > '9') && character != '_' {
			return false
		}
	}
	return true
}

func hasHiddenPathSegment(path string) bool {
	for _, segment := range strings.Split(path, "/") {
		if strings.HasPrefix(segment, ".") {
			return true
		}
	}
	return false
}

func pathWithinRoot(root, target string) bool {
	return target == root || strings.HasPrefix(target, root+string(os.PathSeparator))
}

func atJSONEOF(decoder *json.Decoder) bool {
	var extra any
	return errors.Is(decoder.Decode(&extra), io.EOF)
}

func methodNotAllowed(writer http.ResponseWriter, allowed string) {
	writer.Header().Set("Allow", allowed)
	writeError(writer, http.StatusMethodNotAllowed, "method_not_allowed")
}

func writeError(writer http.ResponseWriter, status int, code string) {
	writeJSON(writer, status, map[string]string{"error": code})
}

func writeJSON(writer http.ResponseWriter, status int, value any) {
	payload, err := json.Marshal(value)
	if err != nil {
		http.Error(writer, http.StatusText(http.StatusInternalServerError), http.StatusInternalServerError)
		return
	}
	writer.Header().Set("Content-Type", "application/json")
	writer.WriteHeader(status)
	_, _ = fmt.Fprintln(writer, string(payload))
}
