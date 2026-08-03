package relay

import (
	"crypto/sha256"
	"errors"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

var (
	errProducerAlreadyConnected = errors.New("a producer is already connected")
	errViewerCapacity           = errors.New("room viewer capacity reached")
	errRoomClosing              = errors.New("room is closing")
)

type frame struct {
	generation  uint64
	prepared    *websocket.PreparedMessage
	payloadSize int
	publishedAt time.Time
}

type viewer struct {
	mu               sync.Mutex
	newestGeneration uint64
	updates          chan frame
	conn             *websocket.Conn
}

func newViewer() *viewer {
	return &viewer{updates: make(chan frame, 1)}
}

func (v *viewer) offer(update frame) bool {
	v.mu.Lock()
	defer v.mu.Unlock()
	if update.generation <= v.newestGeneration {
		return false
	}
	v.newestGeneration = update.generation
	replaced := false
	select {
	case <-v.updates:
		replaced = true
	default:
	}
	select {
	case v.updates <- update:
	default:
	}
	return replaced
}

type room struct {
	id           string
	producerHash [sha256.Size]byte
	inviteHashes [][sha256.Size]byte
	maxViewers   int

	mu                 sync.Mutex
	producerActive     bool
	producerGeneration uint64
	producerConn       *websocket.Conn
	viewers            map[*viewer]struct{}
	latest             frame
	latestAt           time.Time
	frameGeneration    uint64
	closing            bool
}

func (r *room) claimProducer() (uint64, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closing {
		return 0, errRoomClosing
	}
	if r.producerActive {
		return 0, errProducerAlreadyConnected
	}
	r.producerActive = true
	r.producerGeneration++
	return r.producerGeneration, nil
}

func (r *room) attachProducer(generation uint64, conn *websocket.Conn) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closing || !r.producerActive || generation != r.producerGeneration {
		return false
	}
	r.producerConn = conn
	return true
}

func (r *room) releaseProducer(generation uint64) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if generation != r.producerGeneration {
		return
	}
	r.producerActive = false
	r.producerConn = nil
}

func (r *room) attachViewer(v *viewer, conn *websocket.Conn) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closing {
		return false
	}
	if _, exists := r.viewers[v]; !exists {
		return false
	}
	v.conn = conn
	return true
}

func (r *room) addViewer(v *viewer, now time.Time, ttl time.Duration) (frame, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.closing {
		return frame{}, errRoomClosing
	}
	if len(r.viewers) >= r.maxViewers {
		return frame{}, errViewerCapacity
	}
	r.viewers[v] = struct{}{}
	if r.latest.prepared == nil || now.Sub(r.latestAt) > ttl {
		return frame{}, nil
	}
	return r.latest, nil
}

func (r *room) removeViewer(v *viewer) {
	r.mu.Lock()
	delete(r.viewers, v)
	r.mu.Unlock()
}

func (r *room) publish(payload []byte, now time.Time) (int, error) {
	// PreparedMessage builds one immutable wire representation which all
	// viewers can safely share. It also caches one compressed representation
	// per negotiated compression setting, avoiding per-viewer JSON framing and
	// deflate work.
	prepared, err := websocket.NewPreparedMessage(websocket.TextMessage, payload)
	if err != nil {
		return 0, err
	}
	r.mu.Lock()
	r.frameGeneration++
	update := frame{
		generation:  r.frameGeneration,
		prepared:    prepared,
		payloadSize: len(payload),
		publishedAt: now,
	}
	r.latest = update
	r.latestAt = now
	viewers := make([]*viewer, 0, len(r.viewers))
	for v := range r.viewers {
		viewers = append(viewers, v)
	}
	r.mu.Unlock()

	dropped := 0
	for _, v := range viewers {
		if v.offer(update) {
			dropped++
		}
	}
	return dropped, nil
}

func (r *room) expire(now time.Time, ttl time.Duration) {
	r.mu.Lock()
	if r.latest.prepared != nil && now.Sub(r.latestAt) > ttl {
		r.latest = frame{}
		r.latestAt = time.Time{}
	}
	r.mu.Unlock()
}

func (r *room) operationalCounts() (producerActive bool, viewers int, latestFrame bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.producerActive, len(r.viewers), r.latest.prepared != nil
}

func (r *room) closeConnections(deadline time.Time) {
	r.mu.Lock()
	r.closing = true
	producer := r.producerConn
	viewers := make([]*websocket.Conn, 0, len(r.viewers))
	for v := range r.viewers {
		if v.conn != nil {
			viewers = append(viewers, v.conn)
		}
	}
	r.mu.Unlock()

	message := websocket.FormatCloseMessage(websocket.CloseGoingAway, "server shutdown")
	if producer != nil {
		_ = producer.WriteControl(websocket.CloseMessage, message, deadline)
		_ = producer.Close()
	}
	for _, conn := range viewers {
		_ = conn.WriteControl(websocket.CloseMessage, message, deadline)
		_ = conn.Close()
	}
}
