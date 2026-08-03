package relay

import (
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

type bucket struct {
	tokens float64
	last   time.Time
}

type ipLimiter struct {
	mu         sync.Mutex
	rate       float64
	burst      float64
	maxEntries int
	entries    map[string]bucket
	calls      uint64
}

func newIPLimiter(perMinute float64, burst, maxEntries int) *ipLimiter {
	return &ipLimiter{
		rate:       perMinute / 60,
		burst:      float64(burst),
		maxEntries: maxEntries,
		entries:    make(map[string]bucket),
	}
}

func (l *ipLimiter) allow(key string, now time.Time) bool {
	l.mu.Lock()
	defer l.mu.Unlock()

	l.calls++
	if l.calls%256 == 0 {
		l.prune(now.Add(-10 * time.Minute))
	}
	entry, exists := l.entries[key]
	if !exists {
		if len(l.entries) >= l.maxEntries {
			l.prune(now.Add(-time.Minute))
			if len(l.entries) >= l.maxEntries {
				return false
			}
		}
		entry = bucket{tokens: l.burst, last: now}
	}
	if now.After(entry.last) {
		entry.tokens += now.Sub(entry.last).Seconds() * l.rate
		if entry.tokens > l.burst {
			entry.tokens = l.burst
		}
		entry.last = now
	}
	if entry.tokens < 1 {
		l.entries[key] = entry
		return false
	}
	entry.tokens--
	l.entries[key] = entry
	return true
}

func (l *ipLimiter) prune(before time.Time) {
	for key, entry := range l.entries {
		if entry.last.Before(before) {
			delete(l.entries, key)
		}
	}
}

type frameLimiter struct {
	rate   float64
	burst  float64
	tokens float64
	last   time.Time
}

func newFrameLimiter(hz float64, burst int, now time.Time) frameLimiter {
	return frameLimiter{rate: hz, burst: float64(burst), tokens: float64(burst), last: now}
}

func (l *frameLimiter) allow(now time.Time) bool {
	if now.After(l.last) {
		l.tokens += now.Sub(l.last).Seconds() * l.rate
		if l.tokens > l.burst {
			l.tokens = l.burst
		}
		l.last = now
	}
	if l.tokens < 1 {
		return false
	}
	l.tokens--
	return true
}

type clientIPResolver struct {
	trusted []*net.IPNet
}

func newClientIPResolver(cidrs []string) clientIPResolver {
	resolver := clientIPResolver{}
	for _, value := range cidrs {
		_, network, err := net.ParseCIDR(value)
		if err == nil {
			resolver.trusted = append(resolver.trusted, network)
		}
	}
	return resolver
}

func (r clientIPResolver) resolve(request *http.Request) string {
	host, _, err := net.SplitHostPort(request.RemoteAddr)
	if err != nil {
		host = request.RemoteAddr
	}
	peer := net.ParseIP(strings.TrimSpace(host))
	if peer == nil {
		return "unknown"
	}
	if r.isTrusted(peer) {
		// X-Real-IP is deliberately a single address. The reverse proxy must
		// overwrite, not append, this header.
		forwarded := net.ParseIP(strings.TrimSpace(request.Header.Get("X-Real-IP")))
		if forwarded != nil {
			return forwarded.String()
		}
	}
	return peer.String()
}

func (r clientIPResolver) isTrusted(ip net.IP) bool {
	for _, network := range r.trusted {
		if network.Contains(ip) {
			return true
		}
	}
	return false
}
