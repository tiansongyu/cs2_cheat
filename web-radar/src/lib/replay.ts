import { parseServerMessage, type SnapshotMessage } from '../types/protocol';

export const MAX_RECORDING_BYTES = 256 * 1024 * 1024;
export const MAX_RECORDING_FRAMES = 100_000;

export function parseSnapshotRecording(
  text: string,
  maximumFrames = MAX_RECORDING_FRAMES,
): SnapshotMessage[] {
  const frames: SnapshotMessage[] = [];
  let previousSequence = -1;
  for (const line of text.split(/\r?\n/)) {
    if (!line.trim()) continue;
    const message = parseServerMessage(line);
    if (!message || message.type !== 'snapshot') {
      throw new Error(`第 ${frames.length + 1} 个快照不是有效的 v1 JSON`);
    }
    if (message.seq <= previousSequence) {
      throw new Error('快照序号必须严格递增');
    }
    frames.push(message);
    previousSequence = message.seq;
    if (frames.length > maximumFrames) {
      throw new Error(`录制文件超过 ${maximumFrames} 帧上限`);
    }
  }
  if (frames.length === 0) throw new Error('录制文件中没有有效快照');
  return frames;
}

export function replayDelayMs(
  current: SnapshotMessage,
  next: SnapshotMessage,
): number {
  const recordedDelay = next.capturedAtMs - current.capturedAtMs;
  return Number.isFinite(recordedDelay)
    ? Math.min(1_000, Math.max(10, recordedDelay))
    : 50;
}
