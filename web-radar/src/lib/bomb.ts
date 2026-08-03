import type { BombSnapshot } from '../types/protocol';

export interface HostClockAnchor {
  capturedAtMs: number;
  receivedAtPerformanceMs: number;
}

export interface BombTiming {
  explosionRemainingMs: number | null;
  defuseRemainingMs: number | null;
  canCompleteDefuse: boolean | null;
}

export function estimateHostNowMs(anchor: HostClockAnchor, performanceNowMs: number): number {
  const elapsed = Math.max(0, performanceNowMs - anchor.receivedAtPerformanceMs);
  return anchor.capturedAtMs + elapsed;
}

export function getBombTiming(
  bomb: BombSnapshot,
  hostNowMs: number,
  elapsedSinceSnapshotMs = 0,
): BombTiming {
  if (bomb.state !== 'planted') {
    return {
      explosionRemainingMs: null,
      defuseRemainingMs: null,
      canCompleteDefuse: null,
    };
  }

  const elapsed = Math.max(0, elapsedSinceSnapshotMs);
  const explosionRemainingMs = Number.isFinite(bomb.explodeInSeconds)
    ? Math.max(0, (bomb.explodeInSeconds as number) * 1000 - elapsed)
    : Number.isFinite(bomb.explodeAtMs)
      ? Math.max(0, (bomb.explodeAtMs as number) - hostNowMs)
      : null;
  const defuseRemainingMs = bomb.beingDefused && Number.isFinite(bomb.defuseInSeconds)
    ? Math.max(0, (bomb.defuseInSeconds as number) * 1000 - elapsed)
    : bomb.beingDefused && Number.isFinite(bomb.defuseEndAtMs)
      ? Math.max(0, (bomb.defuseEndAtMs as number) - hostNowMs)
      : null;

  const canCompleteDefuse =
    bomb.beingDefused &&
    typeof bomb.defuseWillSucceed === 'boolean'
      ? bomb.defuseWillSucceed
      : bomb.beingDefused &&
          Number.isFinite(bomb.defuseInSeconds) &&
          Number.isFinite(bomb.explodeInSeconds)
        ? (bomb.defuseInSeconds as number) <= (bomb.explodeInSeconds as number)
        : bomb.beingDefused &&
            Number.isFinite(bomb.defuseEndAtMs) &&
            Number.isFinite(bomb.explodeAtMs)
          ? (bomb.defuseEndAtMs as number) <= (bomb.explodeAtMs as number)
          : null;

  return { explosionRemainingMs, defuseRemainingMs, canCompleteDefuse };
}

export function formatCountdown(milliseconds: number | null): string {
  if (milliseconds === null) return '--.-';
  return (Math.max(0, milliseconds) / 1000).toFixed(1);
}
