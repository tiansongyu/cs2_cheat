package relay

import (
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

type sessionViewer struct {
	conn *websocket.Conn
}

type session struct {
	room      *room
	expiresAt time.Time

	mu      sync.Mutex
	revoked bool
	viewers map[*sessionViewer]struct{}
	done    chan struct{}
}

func newSession(currentRoom *room, expiresAt time.Time) *session {
	return &session{
		room:      currentRoom,
		expiresAt: expiresAt,
		viewers:   make(map[*sessionViewer]struct{}),
		done:      make(chan struct{}),
	}
}

func (s *session) valid(now time.Time) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	return !s.revoked && s.expiresAt.After(now)
}

func (s *session) reserveViewer(now time.Time, maximum int) (*sessionViewer, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.revoked || !s.expiresAt.After(now) || len(s.viewers) >= maximum {
		return nil, false
	}
	reservation := &sessionViewer{}
	s.viewers[reservation] = struct{}{}
	return reservation, true
}

func (s *session) attachViewer(reservation *sessionViewer, conn *websocket.Conn, now time.Time) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.revoked || !s.expiresAt.After(now) {
		return false
	}
	if _, exists := s.viewers[reservation]; !exists {
		return false
	}
	reservation.conn = conn
	return true
}

func (s *session) releaseViewer(reservation *sessionViewer) {
	s.mu.Lock()
	delete(s.viewers, reservation)
	s.mu.Unlock()
}

func (s *session) revoke(code int, reason string) {
	s.revokeAt(code, reason, time.Now().Add(time.Second))
}

func (s *session) revokeAt(code int, reason string, deadline time.Time) {
	s.mu.Lock()
	if s.revoked {
		s.mu.Unlock()
		return
	}
	s.revoked = true
	close(s.done)
	connections := make([]*websocket.Conn, 0, len(s.viewers))
	for currentViewer := range s.viewers {
		if currentViewer.conn != nil {
			connections = append(connections, currentViewer.conn)
		}
	}
	s.mu.Unlock()

	message := websocket.FormatCloseMessage(code, reason)
	for _, conn := range connections {
		_ = conn.WriteControl(websocket.CloseMessage, message, deadline)
		_ = conn.Close()
	}
}
