import { createElement } from 'react';
import TestRenderer, { act, type ReactTestRenderer } from 'react-test-renderer';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { RadarDeploymentMode } from '../lib/deployment';
import { useRadarStream, type RadarStreamState } from './useRadarStream';

class MockWebSocket extends EventTarget {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
  static instances: MockWebSocket[] = [];

  readonly url: string;
  readyState = MockWebSocket.CONNECTING;
  throwOnConnectingClose = false;

  constructor(url: string | URL) {
    super();
    this.url = String(url);
    MockWebSocket.instances.push(this);
  }

  close(code = 1000, reason = ''): void {
    if (this.readyState === MockWebSocket.CLOSED) return;
    if (this.throwOnConnectingClose && this.readyState === MockWebSocket.CONNECTING) {
      throw new DOMException('handshake pending', 'InvalidStateError');
    }
    this.emitClose(code, reason);
  }

  emitOpen(): void {
    this.readyState = MockWebSocket.OPEN;
    this.dispatchEvent(new Event('open'));
  }

  emitMessage(data: string): void {
    const event = new Event('message');
    Object.defineProperty(event, 'data', { value: data });
    this.dispatchEvent(event);
  }

  emitClose(code = 1006, reason = ''): void {
    this.readyState = MockWebSocket.CLOSED;
    const event = new Event('close');
    Object.defineProperties(event, {
      code: { value: code },
      reason: { value: reason },
    });
    this.dispatchEvent(event);
  }
}

interface TestBrowser {
  window: Window;
  document: Document;
  navigator: Navigator & { onLine: boolean };
}

function installBrowser(): TestBrowser {
  const windowTarget = new EventTarget() as Window;
  Object.assign(windowTarget, {
    location: { protocol: 'https:', host: 'radar.example.test', search: '' },
    setTimeout: (handler: TimerHandler, delay?: number) => globalThis.setTimeout(handler, delay),
    clearTimeout: (timer: number) => globalThis.clearTimeout(timer),
  });
  const documentTarget = new EventTarget() as Document;
  Object.defineProperty(documentTarget, 'visibilityState', {
    configurable: true,
    value: 'visible',
  });
  const navigatorTarget = { onLine: true } as Navigator & { onLine: boolean };

  vi.stubGlobal('window', windowTarget);
  vi.stubGlobal('document', documentTarget);
  vi.stubGlobal('navigator', navigatorTarget);
  vi.stubGlobal('WebSocket', MockWebSocket);
  Object.assign(globalThis, { IS_REACT_ACT_ENVIRONMENT: true });
  return { window: windowTarget, document: documentTarget, navigator: navigatorTarget };
}

function mountStream(mode: RadarDeploymentMode = 'embedded') {
  let latest: RadarStreamState | undefined;
  let renderer: ReactTestRenderer | undefined;
  const onSessionRejected = vi.fn();

  function Harness() {
    latest = useRadarStream({ mode, onSessionRejected });
    return null;
  }

  act(() => {
    renderer = TestRenderer.create(createElement(Harness));
  });
  return {
    get state() {
      if (!latest) throw new Error('hook did not render');
      return latest;
    },
    onSessionRejected,
    unmount() {
      act(() => renderer?.unmount());
    },
  };
}

function validSnapshot(sequence = 1): string {
  return JSON.stringify({
    v: 1,
    type: 'snapshot',
    seq: sequence,
    capturedAtMs: Date.now(),
    map: { id: 'de_dust2' },
    players: [],
    bomb: { state: 'unknown' },
  });
}

let mounted: ReturnType<typeof mountStream> | undefined;

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date('2026-08-03T00:00:00Z'));
  vi.spyOn(Math, 'random').mockReturnValue(0.5);
  MockWebSocket.instances = [];
  installBrowser();
});

afterEach(() => {
  mounted?.unmount();
  mounted = undefined;
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.useRealTimers();
  delete (globalThis as typeof globalThis & { IS_REACT_ACT_ENVIRONMENT?: boolean })
    .IS_REACT_ACT_ENVIRONMENT;
});

describe('useRadarStream lifecycle', () => {
  it('batches repeated manual retries and keeps only one active socket', () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];

    act(() => {
      mounted?.state.retry();
      mounted?.state.retry();
    });

    expect(MockWebSocket.instances).toHaveLength(2);
    expect(first.readyState).toBe(MockWebSocket.CLOSED);
    expect(MockWebSocket.instances.filter((socket) => socket.readyState !== MockWebSocket.CLOSED))
      .toHaveLength(1);
  });

  it('ignores a late close event from a superseded socket', async () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];
    act(() => mounted?.state.retry());
    expect(MockWebSocket.instances).toHaveLength(2);

    act(() => first.emitClose(1006));
    await act(async () => {
      await vi.advanceTimersByTimeAsync(600);
    });

    expect(MockWebSocket.instances).toHaveLength(2);
  });

  it('stops reconnecting while offline and reconnects immediately when online', async () => {
    const browser = installBrowser();
    mounted = mountStream();
    const first = MockWebSocket.instances[0];

    browser.navigator.onLine = false;
    act(() => {
      browser.window.dispatchEvent(new Event('offline'));
    });
    expect(mounted.state.status).toBe('offline');
    expect(first.readyState).toBe(MockWebSocket.CLOSED);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(60_000);
    });
    expect(MockWebSocket.instances).toHaveLength(1);

    browser.navigator.onLine = true;
    act(() => {
      browser.window.dispatchEvent(new Event('online'));
    });
    expect(MockWebSocket.instances).toHaveLength(2);
    expect(mounted.state.status).toBe('connecting');
  });

  it('aborts an in-flight session probe when the connection is manually replaced', async () => {
    let probeSignal: AbortSignal | undefined;
    let rejectProbe: ((reason: unknown) => void) | undefined;
    vi.stubGlobal('fetch', vi.fn((_input: RequestInfo | URL, init?: RequestInit) => (
      new Promise<Response>((_resolve, reject) => {
        probeSignal = init?.signal ?? undefined;
        rejectProbe = reject;
      })
    )));
    mounted = mountStream('relay');

    act(() => MockWebSocket.instances[0].emitClose(1006));
    expect(probeSignal?.aborted).toBe(false);

    await act(async () => {
      mounted?.state.retry();
      await Promise.resolve();
    });
    expect(probeSignal?.aborted).toBe(true);
    expect(MockWebSocket.instances).toHaveLength(2);

    await act(async () => {
      rejectProbe?.(new TypeError('late network failure'));
      await Promise.resolve();
      await vi.advanceTimersByTimeAsync(600);
    });
    expect(MockWebSocket.instances).toHaveLength(2);
  });

  it('marks a valid snapshot stale after three seconds without polling renders', async () => {
    mounted = mountStream();
    const socket = MockWebSocket.instances[0];
    act(() => {
      socket.emitOpen();
      socket.emitMessage(validSnapshot());
    });
    expect(mounted.state.stale).toBe(false);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(2_999);
    });
    expect(mounted.state.stale).toBe(false);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
    });
    expect(mounted.state.stale).toBe(true);
  });

  it('reports update rate, snapshot age and skipped sequences', () => {
    mounted = mountStream();
    const socket = MockWebSocket.instances[0];
    act(() => {
      socket.emitOpen();
      socket.emitMessage(validSnapshot(10));
      vi.setSystemTime(new Date(Date.now() + 50));
      socket.emitMessage(validSnapshot(13));
    });
    expect(mounted.state.quality.updateRateHz).toBeCloseTo(20);
    expect(mounted.state.quality.skippedFrames).toBe(2);
    expect(mounted.state.quality.snapshotAgeMs).toBe(0);
  });

  it('replaces a WebSocket handshake that remains CONNECTING for ten seconds', async () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];

    await act(async () => {
      await vi.advanceTimersByTimeAsync(9_999);
    });
    expect(MockWebSocket.instances).toHaveLength(1);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
    });
    expect(first.readyState).toBe(MockWebSocket.CLOSED);
    expect(mounted.state.error).toBe('Radar 实时连接超时');
    expect(mounted.state.status).toBe('reconnecting');

    await act(async () => {
      await vi.advanceTimersByTimeAsync(500);
    });
    expect(MockWebSocket.instances).toHaveLength(2);
  });

  it('closes a timed-out candidate if it opens after CONNECTING close threw', async () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];
    first.throwOnConnectingClose = true;

    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_500);
    });
    expect(first.readyState).toBe(MockWebSocket.CONNECTING);
    expect(MockWebSocket.instances).toHaveLength(2);

    act(() => first.emitOpen());
    expect(first.readyState).toBe(MockWebSocket.CLOSED);
    expect(MockWebSocket.instances.filter((socket) => socket.readyState !== MockWebSocket.CLOSED))
      .toHaveLength(1);
  });

  it('closes a superseded manual-retry socket when its late handshake opens', () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];
    first.throwOnConnectingClose = true;

    act(() => mounted?.state.retry());
    expect(first.readyState).toBe(MockWebSocket.CONNECTING);
    expect(MockWebSocket.instances).toHaveLength(2);

    act(() => first.emitOpen());
    expect(first.readyState).toBe(MockWebSocket.CLOSED);
    expect(MockWebSocket.instances.filter((socket) => socket.readyState !== MockWebSocket.CLOSED))
      .toHaveLength(1);
  });

  it('recovers an overdue handshake immediately when a throttled page resumes', () => {
    mounted = mountStream();
    const first = MockWebSocket.instances[0];
    vi.setSystemTime(new Date(Date.now() + 11_000));

    act(() => {
      window.dispatchEvent(new Event('pageshow'));
    });

    expect(first.readyState).toBe(MockWebSocket.CLOSED);
    expect(MockWebSocket.instances).toHaveLength(2);
    expect(mounted.state.status).toBe('connecting');
  });
});
