import type { RadarDeploymentMode } from './deployment';

export type StreamStatus = 'connecting' | 'connected' | 'reconnecting' | 'offline' | 'disconnected';

interface LocationLike {
  protocol: string;
  host: string;
  search: string;
}

export function deriveWebSocketUrl(
  location: LocationLike,
  mode: RadarDeploymentMode = 'embedded',
): string {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const endpoint = `${scheme}//${location.host}/api/v1/stream`;
  if (mode === 'relay') return endpoint;

  const token = new URLSearchParams(location.search).get('token');
  if (!token) return endpoint;
  return `${endpoint}?${new URLSearchParams({ token }).toString()}`;
}

export function isSessionRejectedCloseCode(code: number): boolean {
  return code === 4401 || code === 4403;
}

export function shouldProbeSessionAfterClose(mode: RadarDeploymentMode, code: number): boolean {
  return mode === 'relay' && code === 1006;
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
