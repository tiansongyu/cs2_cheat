import { useCallback, useEffect, useRef, useState } from 'react';
import { isMapManifest, type MapManifest } from '../types/protocol';

interface ManifestState {
  manifest: MapManifest | null;
  loading: boolean;
  error: string | null;
}

interface MapManifestState extends ManifestState {
  retry: () => void;
}

export function useMapManifest(): MapManifestState {
  const [state, setState] = useState<ManifestState>({
    manifest: null,
    loading: true,
    error: null,
  });
  const [requestVersion, setRequestVersion] = useState(0);
  const retryAttempt = useRef(0);

  const retry = useCallback(() => {
    retryAttempt.current = 0;
    setRequestVersion((version) => version + 1);
  }, []);

  useEffect(() => {
    const controller = new AbortController();
    let retryTimer: number | undefined;
    setState((current) => ({ ...current, loading: current.manifest === null, error: null }));
    // Revalidate the small manifest so a corrected deployment is observed;
    // the much larger map PNGs keep their normal browser cache behavior.
    fetch('/maps/manifest.json', { cache: 'no-cache', signal: controller.signal })
      .then(async (response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json() as Promise<unknown>;
      })
      .then((value) => {
        if (!isMapManifest(value)) throw new Error('manifest 格式不符合 v1');
        retryAttempt.current = 0;
        setState({ manifest: value, loading: false, error: null });
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return;
        const message = reason instanceof Error ? reason.message : '未知错误';
        setState((current) => ({ ...current, loading: false, error: message }));
        const delay = Math.min(30_000, 1_000 * (2 ** retryAttempt.current++));
        retryTimer = window.setTimeout(() => {
          setRequestVersion((version) => version + 1);
        }, delay);
      });

    return () => {
      controller.abort();
      if (retryTimer !== undefined) window.clearTimeout(retryTimer);
    };
  }, [requestVersion]);

  useEffect(() => {
    if (!state.error) return undefined;
    const resume = () => {
      if (document.visibilityState !== 'hidden' && navigator.onLine !== false) retry();
    };
    window.addEventListener('online', resume);
    window.addEventListener('pageshow', resume);
    document.addEventListener('visibilitychange', resume);
    return () => {
      window.removeEventListener('online', resume);
      window.removeEventListener('pageshow', resume);
      document.removeEventListener('visibilitychange', resume);
    };
  }, [retry, state.error]);

  return { ...state, retry };
}
