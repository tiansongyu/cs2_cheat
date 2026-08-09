import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { RadarDeploymentMode } from '../lib/deployment';
import { probeRelaySession } from '../lib/session';
import {
  deriveWebSocketUrl,
  isSessionRejectedCloseCode,
  reconnectDelayMs,
  shouldProbeSessionAfterClose,
} from '../lib/stream';
import { parseServerMessage, type SnapshotMessage } from '../types/protocol';
import type { StreamStatus } from '../lib/stream';

export interface RadarFrame {
  snapshot: SnapshotMessage;
  receivedAtPerformanceMs: number;
  receivedAtWallMs: number;
}

export interface RadarStreamState {
  status: StreamStatus;
  frame: RadarFrame | null;
  stale: boolean;
  error: string | null;
  retryInMs: number | null;
  retry: () => void;
  quality: RadarStreamQuality;
}

export interface RadarStreamQuality {
  updateRateHz: number;
  snapshotAgeMs: number;
  skippedFrames: number;
}

interface RadarStreamOptions {
  mode: RadarDeploymentMode;
  onSessionRejected?: () => void;
}

export function useRadarStream({ mode, onSessionRejected }: RadarStreamOptions): RadarStreamState {
  const url = useMemo(() => deriveWebSocketUrl(window.location, mode), [mode]);
  const [status, setStatus] = useState<StreamStatus>('connecting');
  const [frame, setFrame] = useState<RadarFrame | null>(null);
  const [stale, setStale] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [retryInMs, setRetryInMs] = useState<number | null>(null);
  const [retryVersion, setRetryVersion] = useState(0);
  const [quality, setQuality] = useState<RadarStreamQuality>({
    updateRateHz: 0,
    snapshotAgeMs: 0,
    skippedFrames: 0,
  });
  const attemptRef = useRef(0);
  const lastReceivedAtRef = useRef<number | null>(null);

  const retry = useCallback(() => setRetryVersion((version) => version + 1), []);

  useEffect(() => {
    let disposed = false;
    let reconnectTimer: number | undefined;
    let staleTimer: number | undefined;
    let connectWatchdogTimer: number | undefined;
    let socket: WebSocket | undefined;
    let connectionStartedAtMs: number | null = null;
    let sessionProbeController: AbortController | undefined;
    let generation = 0;
    let previousSequence: number | null = null;
    let previousFrameAtMs: number | null = null;
    attemptRef.current = 0;

    const isOffline = () => navigator.onLine === false;
    const closeSocket = (target: WebSocket, code: number, reason: string) => {
      try {
        target.close(code, reason);
      } catch {
        // A CONNECTING socket can reject close() in some engines. Its late
        // open handler will try again after the handshake finishes.
      }
    };

    const clearReconnect = (updateState = true) => {
      if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
      reconnectTimer = undefined;
      if (updateState) setRetryInMs(null);
    };

    const clearConnectWatchdog = () => {
      if (connectWatchdogTimer !== undefined) window.clearTimeout(connectWatchdogTimer);
      connectWatchdogTimer = undefined;
    };

    const refreshStaleness = () => {
      if (staleTimer !== undefined) window.clearTimeout(staleTimer);
      staleTimer = undefined;
      const lastReceivedAt = lastReceivedAtRef.current;
      if (lastReceivedAt === null) {
        setStale(true);
        return;
      }
      const remaining = 3_000 - (Date.now() - lastReceivedAt);
      if (remaining <= 0) {
        setStale(true);
        return;
      }
      setStale(false);
      staleTimer = window.setTimeout(() => setStale(true), remaining);
    };

    const markOffline = () => {
      clearReconnect();
      clearConnectWatchdog();
      sessionProbeController?.abort();
      sessionProbeController = undefined;
      generation += 1;
      const previous = socket;
      socket = undefined;
      connectionStartedAtMs = null;
      if (previous?.readyState === WebSocket.OPEN || previous?.readyState === WebSocket.CONNECTING) {
        closeSocket(previous, 1000, 'network offline');
      }
      setStatus('offline');
      setError('设备已离线；网络恢复后会自动重连');
      refreshStaleness();
    };

    const scheduleReconnect = (immediate = false) => {
      if (disposed) return;
      if (isOffline()) {
        markOffline();
        return;
      }
      clearReconnect();
      setStatus('reconnecting');
      const delay = immediate ? 0 : reconnectDelayMs(attemptRef.current++);
      setRetryInMs(delay);
      reconnectTimer = window.setTimeout(connect, delay);
    };

    const rejectSession = () => {
      if (disposed) return;
      clearConnectWatchdog();
      setStatus('disconnected');
      setRetryInMs(null);
      setError('公网 Radar 会话已失效');
      onSessionRejected?.();
    };

    function connect() {
      if (disposed) return;
      clearReconnect();
      if (isOffline()) {
        markOffline();
        return;
      }
      sessionProbeController?.abort();
      sessionProbeController = undefined;
      clearConnectWatchdog();
      const connectionGeneration = ++generation;
      setStatus(attemptRef.current === 0 ? 'connecting' : 'reconnecting');
      let candidate: WebSocket;
      try {
        candidate = new WebSocket(url);
      } catch {
        setError('无法创建 Radar WebSocket 连接');
        scheduleReconnect();
        return;
      }
      socket = candidate;
      connectionStartedAtMs = Date.now();
      connectWatchdogTimer = window.setTimeout(() => {
        if (
          disposed
          || connectionGeneration !== generation
          || candidate.readyState !== WebSocket.CONNECTING
        ) {
          return;
        }
        connectWatchdogTimer = undefined;
        generation += 1;
        if (socket === candidate) socket = undefined;
        connectionStartedAtMs = null;
        closeSocket(candidate, 4001, 'connection timeout');
        setError('Radar 实时连接超时');
        scheduleReconnect();
      }, 10_000);

      candidate.addEventListener('open', () => {
        if (disposed || connectionGeneration !== generation) {
          closeSocket(
            candidate,
            disposed ? 1000 : 4000,
            disposed ? 'page closed' : 'superseded connection',
          );
          return;
        }
        clearConnectWatchdog();
        connectionStartedAtMs = null;
        setStatus('connected');
        setRetryInMs(null);
        setError(null);
      });

      candidate.addEventListener('message', (event) => {
        if (disposed || connectionGeneration !== generation) return;
        if (typeof event.data !== 'string') {
          setError('收到不支持的二进制数据帧');
          return;
        }
        const message = parseServerMessage(event.data);
        if (!message) {
          setError('收到无法识别的 v1 数据帧');
          return;
        }
        if (message.type === 'error') {
          setError(`${message.code}: ${message.message}`);
          return;
        }
        if (message.type !== 'snapshot') return;

        const receivedAtWallMs = Date.now();
        const intervalMs = previousFrameAtMs === null
          ? null
          : receivedAtWallMs - previousFrameAtMs;
        const sequenceGap = previousSequence !== null && message.seq > previousSequence
          ? Math.max(0, message.seq - previousSequence - 1)
          : 0;
        setQuality((current) => ({
          updateRateHz: intervalMs !== null && intervalMs > 0
            ? current.updateRateHz === 0
              ? 1_000 / intervalMs
              : current.updateRateHz * 0.8 + (1_000 / intervalMs) * 0.2
            : current.updateRateHz,
          snapshotAgeMs: Math.max(0, receivedAtWallMs - message.capturedAtMs),
          skippedFrames: current.skippedFrames + sequenceGap,
        }));
        previousSequence = message.seq;
        previousFrameAtMs = receivedAtWallMs;
        lastReceivedAtRef.current = receivedAtWallMs;
        attemptRef.current = 0;
        setFrame({
          snapshot: message,
          receivedAtPerformanceMs: performance.now(),
          receivedAtWallMs,
        });
        refreshStaleness();
      });

      candidate.addEventListener('close', (event) => {
        if (disposed || connectionGeneration !== generation) return;
        clearConnectWatchdog();
        socket = undefined;
        connectionStartedAtMs = null;
        if (mode === 'relay' && isSessionRejectedCloseCode(event.code)) {
          rejectSession();
          return;
        }
        if (event.reason) setError(event.reason);
        else setError(mode === 'relay' ? '公网 Radar 连接已中断' : '内嵌 Radar 连接已中断');

        if (isOffline()) {
          markOffline();
          return;
        }

        // Browsers deliberately hide the HTTP status of a rejected WebSocket
        // upgrade and report it as 1006. Re-probe the cookie session so an
        // HTTP 401/403 becomes a login prompt, while a temporary network
        // outage keeps the normal reconnect behavior.
        if (shouldProbeSessionAfterClose(mode, event.code)) {
          setStatus('reconnecting');
          sessionProbeController = new AbortController();
          void probeRelaySession(fetch, sessionProbeController.signal)
            .then((probe) => {
              if (disposed || connectionGeneration !== generation) return;
              if (probe.kind !== 'authenticated') {
                rejectSession();
                return;
              }
              scheduleReconnect();
            })
            .catch((probeError: unknown) => {
              if (disposed || connectionGeneration !== generation) return;
              if (probeError instanceof DOMException && probeError.name === 'AbortError') return;
              scheduleReconnect();
            });
          return;
        }

        scheduleReconnect();
      });

      candidate.addEventListener('error', () => {
        if (!disposed && connectionGeneration === generation) {
          setError(mode === 'relay' ? '无法连接公网 Radar Relay' : '无法连接内嵌 Radar 服务');
        }
      });
    }

    const restartNow = () => {
      if (disposed) return;
      if (isOffline()) {
        markOffline();
        return;
      }
      clearReconnect();
      clearConnectWatchdog();
      sessionProbeController?.abort();
      sessionProbeController = undefined;
      generation += 1;
      const previous = socket;
      socket = undefined;
      connectionStartedAtMs = null;
      if (previous?.readyState === WebSocket.OPEN || previous?.readyState === WebSocket.CONNECTING) {
        closeSocket(previous, 4000, 'client retry');
      }
      attemptRef.current = 0;
      connect();
    };

    const handleOnline = () => restartNow();
    const handleOffline = () => markOffline();
    const handleResume = () => {
      if (document.visibilityState === 'hidden') return;
      refreshStaleness();
      if (isOffline()) {
        markOffline();
        return;
      }
      if (
        !socket
        || socket.readyState === WebSocket.CLOSED
        || socket.readyState === WebSocket.CLOSING
      ) {
        restartNow();
        return;
      }
      if (
        socket.readyState === WebSocket.CONNECTING
        && connectionStartedAtMs !== null
        && Date.now() - connectionStartedAtMs >= 10_000
      ) {
        restartNow();
        return;
      }
      const lastReceivedAt = lastReceivedAtRef.current;
      if (
        socket.readyState === WebSocket.OPEN
        && lastReceivedAt !== null
        && Date.now() - lastReceivedAt > 10_000
      ) {
        restartNow();
      }
    };

    refreshStaleness();
    connect();
    window.addEventListener('online', handleOnline);
    window.addEventListener('offline', handleOffline);
    window.addEventListener('pageshow', handleResume);
    window.addEventListener('focus', handleResume);
    document.addEventListener('visibilitychange', handleResume);
    return () => {
      disposed = true;
      generation += 1;
      sessionProbeController?.abort();
      clearReconnect(false);
      clearConnectWatchdog();
      if (staleTimer !== undefined) window.clearTimeout(staleTimer);
      window.removeEventListener('online', handleOnline);
      window.removeEventListener('offline', handleOffline);
      window.removeEventListener('pageshow', handleResume);
      window.removeEventListener('focus', handleResume);
      document.removeEventListener('visibilitychange', handleResume);
      if (socket?.readyState === WebSocket.OPEN || socket?.readyState === WebSocket.CONNECTING) {
        closeSocket(socket, 1000, 'page closed');
      }
    };
  }, [mode, onSessionRejected, retryVersion, url]);

  return {
    status,
    frame,
    stale,
    error,
    retryInMs,
    retry,
    quality,
  };
}
