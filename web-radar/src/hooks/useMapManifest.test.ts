import { createElement } from 'react';
import TestRenderer, { act, type ReactTestRenderer } from 'react-test-renderer';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { useMapManifest } from './useMapManifest';

const manifest = {
  version: 1,
  source: { project: 'test/maps', release: '42' },
  maps: [{
    id: 'de_dust2',
    name: 'Dust II',
    origin: { x: -2_473, y: 3_238 },
    scale: 4.4,
    image: '/maps/de_dust2/radar.png',
  }],
};
const source = {
  release: '42',
  images_sha256: 'a'.repeat(64),
  map_data_sha256: 'b'.repeat(64),
};

function successfulResponse(input: RequestInfo | URL): Response {
  return new Response(
    JSON.stringify(String(input).includes('SOURCE.json') ? source : manifest),
    { status: 200 },
  );
}

function installBrowser() {
  const windowTarget = new EventTarget() as Window;
  Object.assign(windowTarget, {
    setTimeout: (handler: TimerHandler, delay?: number) => globalThis.setTimeout(handler, delay),
    clearTimeout: (timer: number) => globalThis.clearTimeout(timer),
  });
  const documentTarget = new EventTarget() as Document;
  Object.defineProperty(documentTarget, 'visibilityState', { value: 'visible' });
  vi.stubGlobal('window', windowTarget);
  vi.stubGlobal('document', documentTarget);
  vi.stubGlobal('navigator', { onLine: true });
  Object.assign(globalThis, { IS_REACT_ACT_ENVIRONMENT: true });
  return windowTarget;
}

let renderer: ReactTestRenderer | undefined;
let latest: ReturnType<typeof useMapManifest> | undefined;

async function mountManifest() {
  function Harness() {
    latest = useMapManifest();
    return null;
  }
  await act(async () => {
    renderer = TestRenderer.create(createElement(Harness));
    await Promise.resolve();
  });
}

beforeEach(() => {
  vi.useFakeTimers();
  installBrowser();
  latest = undefined;
});

afterEach(() => {
  act(() => renderer?.unmount());
  renderer = undefined;
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.useRealTimers();
  delete (globalThis as typeof globalThis & { IS_REACT_ACT_ENVIRONMENT?: boolean })
    .IS_REACT_ACT_ENVIRONMENT;
});

describe('useMapManifest', () => {
  it('revalidates the manifest while leaving image caching to the browser', async () => {
    const fetcher = vi.fn(async (input: RequestInfo | URL) => successfulResponse(input));
    vi.stubGlobal('fetch', fetcher);
    await mountManifest();

    expect(fetcher).toHaveBeenCalledWith('/maps/manifest.json', expect.objectContaining({
      cache: 'no-cache',
      signal: expect.any(AbortSignal),
    }));
    expect(fetcher).toHaveBeenCalledWith('/maps/SOURCE.json', expect.objectContaining({
      cache: 'no-cache',
      signal: expect.any(AbortSignal),
    }));
    expect(latest?.manifest?.maps[0].id).toBe('de_dust2');
    expect(latest?.error).toBeNull();
  });

  it('retries a transient manifest failure with bounded backoff', async () => {
    const fetcher = vi.fn()
      .mockRejectedValueOnce(new TypeError('network unavailable'))
      .mockImplementation(async (input: RequestInfo | URL) => successfulResponse(input));
    vi.stubGlobal('fetch', fetcher);
    await mountManifest();
    expect(latest?.error).toBe('network unavailable');

    await act(async () => {
      await vi.advanceTimersByTimeAsync(999);
    });
    expect(fetcher).toHaveBeenCalledTimes(1);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(1);
      await Promise.resolve();
    });
    expect(fetcher).toHaveBeenCalledTimes(3);
    expect(latest?.manifest?.maps[0].id).toBe('de_dust2');
  });

  it('retries immediately when connectivity returns and cancels the old timer', async () => {
    const browserWindow = window;
    const fetcher = vi.fn()
      .mockRejectedValueOnce(new TypeError('offline'))
      .mockImplementation(async (input: RequestInfo | URL) => successfulResponse(input));
    vi.stubGlobal('fetch', fetcher);
    await mountManifest();

    await act(async () => {
      browserWindow.dispatchEvent(new Event('online'));
      await Promise.resolve();
    });
    expect(fetcher).toHaveBeenCalledTimes(3);
    expect(latest?.error).toBeNull();

    await act(async () => {
      await vi.advanceTimersByTimeAsync(1_000);
    });
    expect(fetcher).toHaveBeenCalledTimes(3);
  });
});
