import { useCallback, useEffect, useMemo, useState } from 'react';
import type { RadarFrame } from './useRadarStream';
import {
  MAX_RECORDING_BYTES,
  parseSnapshotRecording,
  replayDelayMs,
} from '../lib/replay';
import type { SnapshotMessage } from '../types/protocol';

export interface SnapshotReplayState {
  active: boolean;
  playing: boolean;
  frame: RadarFrame | null;
  fileName: string | null;
  current: number;
  total: number;
  error: string | null;
  load: (file: File) => Promise<void>;
  togglePlaying: () => void;
  seek: (index: number) => void;
  stop: () => void;
}

export function useSnapshotReplay(): SnapshotReplayState {
  const [frames, setFrames] = useState<SnapshotMessage[]>([]);
  const [fileName, setFileName] = useState<string | null>(null);
  const [index, setIndex] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(async (file: File) => {
    setPlaying(false);
    setError(null);
    if (file.size > MAX_RECORDING_BYTES) {
      setError('录制文件超过 256 MB 安全上限');
      return;
    }
    try {
      const parsed = parseSnapshotRecording(await file.text());
      setFrames(parsed);
      setFileName(file.name);
      setIndex(0);
    } catch (reason) {
      setFrames([]);
      setFileName(null);
      setError(reason instanceof Error ? reason.message : '无法读取录制文件');
    }
  }, []);

  const stop = useCallback(() => {
    setFrames([]);
    setFileName(null);
    setIndex(0);
    setPlaying(false);
    setError(null);
  }, []);

  const seek = useCallback((next: number) => {
    setIndex(Math.max(0, Math.min(frames.length - 1, Math.trunc(next))));
  }, [frames.length]);

  const togglePlaying = useCallback(() => {
    if (frames.length <= 1) return;
    setPlaying((value) => !value);
  }, [frames.length]);

  useEffect(() => {
    if (!playing || frames.length <= 1) return undefined;
    if (index >= frames.length - 1) {
      setPlaying(false);
      return undefined;
    }
    const timer = window.setTimeout(
      () => setIndex((value) => Math.min(value + 1, frames.length - 1)),
      replayDelayMs(frames[index], frames[index + 1]),
    );
    return () => window.clearTimeout(timer);
  }, [frames, index, playing]);

  const frame = useMemo<RadarFrame | null>(() => {
    const snapshot = frames[index];
    if (!snapshot) return null;
    return {
      snapshot,
      receivedAtPerformanceMs: performance.now(),
      receivedAtWallMs: Date.now(),
    };
  }, [frames, index]);

  return {
    active: frames.length > 0,
    playing,
    frame,
    fileName,
    current: frames.length > 0 ? index + 1 : 0,
    total: frames.length,
    error,
    load,
    togglePlaying,
    seek,
    stop,
  };
}
