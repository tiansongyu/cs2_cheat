import { describe, expect, it } from 'vitest';
import { estimateHostNowMs, formatCountdown, getBombTiming } from './bomb';

describe('host monotonic clock projection', () => {
  it('advances host time using browser performance elapsed time', () => {
    expect(
      estimateHostNowMs(
        { capturedAtMs: 80_000, receivedAtPerformanceMs: 1_000 },
        1_250,
      ),
    ).toBe(80_250);
  });

  it('does not run host time backwards', () => {
    expect(
      estimateHostNowMs(
        { capturedAtMs: 80_000, receivedAtPerformanceMs: 1_000 },
        900,
      ),
    ).toBe(80_000);
  });
});

describe('bomb countdown', () => {
  it('computes explosion and successful defuse deadlines in the host clock domain', () => {
    const timing = getBombTiming(
      {
        state: 'planted',
        explodeAtMs: 50_000,
        beingDefused: true,
        defuseEndAtMs: 48_000,
      },
      45_500,
    );
    expect(timing).toEqual({
      explosionRemainingMs: 4_500,
      defuseRemainingMs: 2_500,
      canCompleteDefuse: true,
    });
  });

  it('reports an impossible defuse and clamps elapsed timers', () => {
    const timing = getBombTiming(
      {
        state: 'planted',
        explodeAtMs: 10_000,
        beingDefused: true,
        defuseEndAtMs: 11_000,
      },
      10_500,
    );
    expect(timing.explosionRemainingMs).toBe(0);
    expect(timing.canCompleteDefuse).toBe(false);
  });

  it('prefers relative server timers and advances them from frame receipt', () => {
    const timing = getBombTiming(
      {
        state: 'planted',
        explodeInSeconds: 12,
        explodeAtMs: 999_999,
        beingDefused: true,
        defuseInSeconds: 7,
        defuseWillSucceed: true,
      },
      1_000,
      1_500,
    );
    expect(timing.explosionRemainingMs).toBe(10_500);
    expect(timing.defuseRemainingMs).toBe(5_500);
    expect(timing.canCompleteDefuse).toBe(true);
  });

  it('hides timers for non-planted states and formats tenths', () => {
    expect(getBombTiming({ state: 'dropped' }, 1_000).explosionRemainingMs).toBeNull();
    expect(formatCountdown(12_349)).toBe('12.3');
    expect(formatCountdown(null)).toBe('--.-');
  });
});
