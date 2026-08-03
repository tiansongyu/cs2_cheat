export type StreamStatus = 'connecting' | 'connected' | 'reconnecting' | 'disconnected';

interface LocationLike {
  protocol: string;
  host: string;
  search: string;
}

export function deriveWebSocketUrl(location: LocationLike): string {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const source = new URLSearchParams(location.search);
  const target = new URLSearchParams();
  const token = source.get('token');
  if (token) target.set('token', token);
  const query = target.toString();
  return `${scheme}//${location.host}/api/v1/stream${query ? `?${query}` : ''}`;
}

export function reconnectDelayMs(attempt: number, jitter = Math.random()): number {
  const base = Math.min(10_000, 500 * 2 ** Math.max(0, attempt));
  const normalizedJitter = Math.min(1, Math.max(0, jitter));
  return Math.round(base * (0.8 + normalizedJitter * 0.4));
}

export function isSnapshotStale(
  receivedAtWallMs: number | null,
  nowWallMs: number,
  thresholdMs = 3_000,
): boolean {
  return receivedAtWallMs === null || nowWallMs - receivedAtWallMs > thresholdMs;
}
