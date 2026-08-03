package relay

import (
	"testing"
	"time"
)

func TestFrameLimiter(t *testing.T) {
	now := time.Unix(100, 0)
	limiter := newFrameLimiter(2, 2, now)
	if !limiter.allow(now) || !limiter.allow(now) || limiter.allow(now) {
		t.Fatal("burst limit was not enforced")
	}
	if !limiter.allow(now.Add(500 * time.Millisecond)) {
		t.Fatal("rate token was not replenished")
	}
}

func TestIPLimiterBoundsTracking(t *testing.T) {
	now := time.Unix(100, 0)
	limiter := newIPLimiter(60, 1, 2)
	if !limiter.allow("a", now) || limiter.allow("a", now) {
		t.Fatal("per-IP burst was not enforced")
	}
	if !limiter.allow("b", now) {
		t.Fatal("second address unexpectedly rejected")
	}
	if limiter.allow("c", now) {
		t.Fatal("tracking capacity was not bounded")
	}
}
