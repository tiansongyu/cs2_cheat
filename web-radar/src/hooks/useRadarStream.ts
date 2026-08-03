import { useEffect, useMemo, useRef, useState } from 'react';
import type { RadarDeploymentMode } from '../lib/deployment';
import { probeRelaySession } from '../lib/session';
import {
  deriveWebSocketUrl,
  isSessionRejectedCloseCode,
  isSnapshotStale,
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
  url: string;
}

interface RadarStreamOptions {
  mode: RadarDeploymentMode;
  onSessionRejected?: () => void;
}

export function useRadarStream({ mode, onSessionRejected }: RadarStreamOptions): RadarStreamState {
  const url = useMemo(() => deriveWebSocketUrl(window.location, mode), [mode]);
  const [status, setStatus] = useState<StreamStatus>('connecting');
  const [frame, setFrame] = useState<RadarFrame | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [clock, setClock] = useState(() => Date.now());
  const attemptRef = useRef(0);

  useEffect(() => {
    const timer = window.setInterval(() => setClock(Date.now()), 750);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    let disposed = false;
    let reconnectTimer: number | undefined;
    let socket: WebSocket | undefined;
    let sessionProbeController: AbortController | undefined;

    const scheduleReconnect = () => {
      if (disposed) return;
      setStatus('reconnecting');
      const delay = reconnectDelayMs(attemptRef.current++);
      reconnectTimer = window.setTimeout(connect, delay);
    };

    const rejectSession = () => {
      if (disposed) return;
      setStatus('disconnected');
      setError('公网 Radar 会话已失效');
      onSessionRejected?.();
    };

    function connect() {
      if (disposed) return;
      setStatus(attemptRef.current === 0 ? 'connecting' : 'reconnecting');
      socket = new WebSocket(url);

      socket.addEventListener('open', () => {
        if (disposed) return;
        attemptRef.current = 0;
        setStatus('connected');
        setError(null);
      });

      socket.addEventListener('message', (event) => {
        if (disposed || typeof event.data !== 'string') return;
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

        setFrame({
          snapshot: message,
          receivedAtPerformanceMs: performance.now(),
          receivedAtWallMs: Date.now(),
        });
        setClock(Date.now());
      });

      socket.addEventListener('close', (event) => {
        if (disposed) return;
        if (mode === 'relay' && isSessionRejectedCloseCode(event.code)) {
          rejectSession();
          return;
        }
        if (event.reason) setError(event.reason);

        // Browsers deliberately hide the HTTP status of a rejected WebSocket
        // upgrade and report it as 1006. Re-probe the cookie session so an
        // HTTP 401/403 becomes a login prompt, while a temporary network
        // outage keeps the normal reconnect behavior.
        if (shouldProbeSessionAfterClose(mode, event.code)) {
          setStatus('reconnecting');
          sessionProbeController = new AbortController();
          void probeRelaySession(fetch, sessionProbeController.signal)
            .then((probe) => {
              if (disposed) return;
              if (probe.kind !== 'authenticated') {
                rejectSession();
                return;
              }
              scheduleReconnect();
            })
            .catch(() => scheduleReconnect());
          return;
        }

        scheduleReconnect();
      });

      socket.addEventListener('error', () => {
        if (!disposed) {
          setError(mode === 'relay' ? '无法连接公网 Radar Relay' : '无法连接内嵌 Radar 服务');
        }
      });
    }

    connect();
    return () => {
      disposed = true;
      sessionProbeController?.abort();
      if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
      if (socket?.readyState === WebSocket.OPEN || socket?.readyState === WebSocket.CONNECTING) {
        socket.close(1000, 'page closed');
      }
    };
  }, [mode, onSessionRejected, url]);

  return {
    status,
    frame,
    stale: isSnapshotStale(frame?.receivedAtWallMs ?? null, clock),
    error,
    url,
  };
}
