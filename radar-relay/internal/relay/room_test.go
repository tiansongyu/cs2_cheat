package relay

import (
	"testing"
	"time"
)

func TestPublishedFrameIsSharedAndSlowViewerQueueIsLatestOnly(t *testing.T) {
	firstViewer := newViewer()
	secondViewer := newViewer()
	currentRoom := &room{
		maxViewers: 2,
		viewers: map[*viewer]struct{}{
			firstViewer:  {},
			secondViewer: {},
		},
	}
	now := time.Now()
	if dropped, err := currentRoom.publish([]byte("first"), now); err != nil || dropped != 0 {
		t.Fatalf("first publish dropped=%d err=%v", dropped, err)
	}
	if dropped, err := currentRoom.publish([]byte("second"), now.Add(time.Millisecond)); err != nil || dropped != 2 {
		t.Fatalf("replacement publish dropped=%d err=%v, want 2", dropped, err)
	}
	first := <-firstViewer.updates
	second := <-secondViewer.updates
	if first.generation != 2 || second.generation != 2 {
		t.Fatalf("viewers did not retain only generation 2: %d, %d", first.generation, second.generation)
	}
	if first.prepared == nil || first.prepared != second.prepared || first.prepared != currentRoom.latest.prepared {
		t.Fatal("viewers did not share the room's immutable prepared message")
	}
	if first.payloadSize != len("second") {
		t.Fatalf("payload size=%d, want %d", first.payloadSize, len("second"))
	}
}
