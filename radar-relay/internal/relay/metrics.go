package relay

import (
	"fmt"
	"net/http"
	"strings"
	"sync/atomic"
)

type relayMetrics struct {
	sessionsCreated        atomic.Uint64
	loginAuthFailures      atomic.Uint64
	loginRateLimited       atomic.Uint64
	producerConnections    atomic.Uint64
	producerAuthFailures   atomic.Uint64
	producerConflicts      atomic.Uint64
	viewerConnections      atomic.Uint64
	viewerAuthFailures     atomic.Uint64
	viewerCapacityRejected atomic.Uint64
	snapshotsPublished     atomic.Uint64
	snapshotsRejected      atomic.Uint64
	snapshotBytes          atomic.Uint64
	viewerFramesSent       atomic.Uint64
	viewerPayloadBytes     atomic.Uint64
	viewerFramesDropped    atomic.Uint64
	websocketWriteErrors   atomic.Uint64
}

type operationalGauges struct {
	sessions    int
	producers   int
	viewers     int
	latestFrame int
}

func (s *Server) operationalGauges() operationalGauges {
	s.sessionsMu.Lock()
	gauges := operationalGauges{sessions: len(s.sessions)}
	s.sessionsMu.Unlock()
	for _, currentRoom := range s.rooms {
		producer, viewers, latest := currentRoom.operationalCounts()
		if producer {
			gauges.producers++
		}
		gauges.viewers += viewers
		if latest {
			gauges.latestFrame++
		}
	}
	return gauges
}

func (s *Server) handleMetrics(writer http.ResponseWriter, request *http.Request) {
	if !s.config.EnableMetrics {
		http.NotFound(writer, request)
		return
	}
	if request.Method != http.MethodGet {
		methodNotAllowed(writer, http.MethodGet)
		return
	}
	gauges := s.operationalGauges()
	ready := 0
	if s.ready.Load() {
		ready = 1
	}

	var output strings.Builder
	writeMetric(&output, "radar_relay_ready", "Whether the relay is accepting new WebSocket connections.", "gauge", uint64(ready))
	writeMetric(&output, "radar_relay_sessions_active", "Current authenticated browser sessions.", "gauge", uint64(gauges.sessions))
	writeMetric(&output, "radar_relay_producers_active", "Current producer reservations and connections.", "gauge", uint64(gauges.producers))
	writeMetric(&output, "radar_relay_viewers_active", "Current viewer reservations and connections.", "gauge", uint64(gauges.viewers))
	writeMetric(&output, "radar_relay_rooms_with_latest_frame", "Rooms currently retaining a latest frame.", "gauge", uint64(gauges.latestFrame))
	writeMetric(&output, "radar_relay_sessions_created_total", "Browser sessions created.", "counter", s.metrics.sessionsCreated.Load())
	writeMetric(&output, "radar_relay_login_auth_failures_total", "Session logins rejected for invalid credentials.", "counter", s.metrics.loginAuthFailures.Load())
	writeMetric(&output, "radar_relay_login_rate_limited_total", "Session logins rejected by the IP rate limiter.", "counter", s.metrics.loginRateLimited.Load())
	writeMetric(&output, "radar_relay_producer_connections_total", "Producer WebSocket connections accepted.", "counter", s.metrics.producerConnections.Load())
	writeMetric(&output, "radar_relay_producer_auth_failures_total", "Producer handshakes rejected for invalid credentials.", "counter", s.metrics.producerAuthFailures.Load())
	writeMetric(&output, "radar_relay_producer_conflicts_total", "Producer handshakes rejected because the room already had a producer.", "counter", s.metrics.producerConflicts.Load())
	writeMetric(&output, "radar_relay_viewer_connections_total", "Viewer WebSocket connections accepted.", "counter", s.metrics.viewerConnections.Load())
	writeMetric(&output, "radar_relay_viewer_auth_failures_total", "Viewer handshakes rejected for an invalid session.", "counter", s.metrics.viewerAuthFailures.Load())
	writeMetric(&output, "radar_relay_viewer_capacity_rejections_total", "Viewer handshakes rejected by room or session capacity.", "counter", s.metrics.viewerCapacityRejected.Load())
	writeMetric(&output, "radar_relay_snapshots_published_total", "Valid producer snapshots accepted.", "counter", s.metrics.snapshotsPublished.Load())
	writeMetric(&output, "radar_relay_snapshots_rejected_total", "Producer snapshots rejected by size, schema, time, type, or rate policy.", "counter", s.metrics.snapshotsRejected.Load())
	writeMetric(&output, "radar_relay_snapshot_bytes_total", "Uncompressed valid snapshot bytes accepted.", "counter", s.metrics.snapshotBytes.Load())
	writeMetric(&output, "radar_relay_viewer_frames_sent_total", "Snapshot messages successfully written to viewers.", "counter", s.metrics.viewerFramesSent.Load())
	writeMetric(&output, "radar_relay_viewer_payload_bytes_total", "Uncompressed snapshot payload bytes successfully written to viewers.", "counter", s.metrics.viewerPayloadBytes.Load())
	writeMetric(&output, "radar_relay_viewer_frames_dropped_total", "Obsolete queued viewer frames replaced by a newer frame.", "counter", s.metrics.viewerFramesDropped.Load())
	writeMetric(&output, "radar_relay_websocket_write_errors_total", "Viewer data or ping writes that failed.", "counter", s.metrics.websocketWriteErrors.Load())

	writer.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
	writer.WriteHeader(http.StatusOK)
	_, _ = writer.Write([]byte(output.String()))
}

func writeMetric(output *strings.Builder, name, help, metricType string, value uint64) {
	_, _ = fmt.Fprintf(output, "# HELP %s %s\n# TYPE %s %s\n%s %d\n", name, help, name, metricType, name, value)
}
