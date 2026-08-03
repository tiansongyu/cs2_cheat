import { useEffect, useMemo, useRef, useState } from 'react';
import { deriveWebSocketUrl, isSnapshotStale, reconnectDelayMs } from '../lib/stream';
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

export function useRadarStream(): RadarStreamState {
  const url = useMemo(() => deriveWebSocketUrl(window.location), []);
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

    const connect = () => {
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
        setStatus('reconnecting');
        if (event.reason) setError(event.reason);
        const delay = reconnectDelayMs(attemptRef.current++);
        reconnectTimer = window.setTimeout(connect, delay);
      });

      socket.addEventListener('error', () => {
        if (!disposed) setError('无法连接内嵌 Radar 服务');
      });
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
      if (socket?.readyState === WebSocket.OPEN || socket?.readyState === WebSocket.CONNECTING) {
        socket.close(1000, 'page closed');
      }
    };
  }, [url]);

  return {
    status,
    frame,
    stale: isSnapshotStale(frame?.receivedAtWallMs ?? null, clock),
    error,
    url,
  };
}
