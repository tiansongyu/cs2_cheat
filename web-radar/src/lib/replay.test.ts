import { describe, expect, it } from 'vitest';
import { parseSnapshotRecording, replayDelayMs } from './replay';

function snapshot(sequence: number, capturedAtMs: number) {
  return {
    v: 1,
    type: 'snapshot',
    seq: sequence,
    capturedAtMs,
    map: { id: 'de_dust2' },
    players: [],
    bomb: { state: 'unknown' },
  };
}

describe('snapshot recording replay', () => {
  it('parses newline-delimited v1 snapshots', () => {
    const frames = parseSnapshotRecording([
      JSON.stringify(snapshot(1, 1000)),
      JSON.stringify(snapshot(2, 1050)),
      '',
    ].join('\n'));
    expect(frames.map((frame) => frame.seq)).toEqual([1, 2]);
    expect(replayDelayMs(frames[0], frames[1])).toBe(50);
  });

  it('rejects malformed and non-monotonic recordings', () => {
    expect(() => parseSnapshotRecording('{}')).toThrow(/v1 JSON/);
    expect(() => parseSnapshotRecording([
      JSON.stringify(snapshot(2, 1000)),
      JSON.stringify(snapshot(2, 1050)),
    ].join('\n'))).toThrow(/严格递增/);
  });

  it('bounds unreasonable playback gaps', () => {
    const frames = parseSnapshotRecording([
      JSON.stringify(snapshot(1, 1000)),
      JSON.stringify(snapshot(2, 20_000)),
    ].join('\n'));
    expect(replayDelayMs(frames[0], frames[1])).toBe(1000);
  });
});
