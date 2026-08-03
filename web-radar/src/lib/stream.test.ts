import { describe, expect, it } from 'vitest';
import { deriveWebSocketUrl, isSnapshotStale, reconnectDelayMs } from './stream';

describe('deriveWebSocketUrl', () => {
  it('uses secure WebSocket on HTTPS and forwards only token', () => {
    expect(
      deriveWebSocketUrl({
        protocol: 'https:',
        host: 'radar.example:8443',
        search: '?token=a%20b&debug=true',
      }),
    ).toBe('wss://radar.example:8443/api/v1/stream?token=a+b');
  });

  it('uses the current HTTP host without an empty query', () => {
    expect(
      deriveWebSocketUrl({ protocol: 'http:', host: '127.0.0.1:22006', search: '' }),
    ).toBe('ws://127.0.0.1:22006/api/v1/stream');
  });
});

describe('stream state helpers', () => {
  it('caps exponential reconnect delay and applies bounded jitter', () => {
    expect(reconnectDelayMs(0, 0.5)).toBe(500);
    expect(reconnectDelayMs(10, 0.5)).toBe(10_000);
    expect(reconnectDelayMs(0, 0)).toBe(400);
    expect(reconnectDelayMs(0, 1)).toBe(600);
  });

  it('marks absent and old snapshots stale', () => {
    expect(isSnapshotStale(null, 10_000)).toBe(true);
    expect(isSnapshotStale(8_000, 10_000)).toBe(false);
    expect(isSnapshotStale(6_000, 10_000)).toBe(true);
  });
});
