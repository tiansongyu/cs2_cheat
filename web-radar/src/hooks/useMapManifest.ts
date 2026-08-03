import { useEffect, useState } from 'react';
import { isMapManifest, type MapManifest } from '../types/protocol';

interface ManifestState {
  manifest: MapManifest | null;
  loading: boolean;
  error: string | null;
}

export function useMapManifest(): ManifestState {
  const [state, setState] = useState<ManifestState>({
    manifest: null,
    loading: true,
    error: null,
  });

  useEffect(() => {
    const controller = new AbortController();
    fetch('/maps/manifest.json', { cache: 'no-cache', signal: controller.signal })
      .then(async (response) => {
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return response.json() as Promise<unknown>;
      })
      .then((value) => {
        if (!isMapManifest(value)) throw new Error('manifest 格式不符合 v1');
        setState({ manifest: value, loading: false, error: null });
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return;
        const message = reason instanceof Error ? reason.message : '未知错误';
        setState({ manifest: null, loading: false, error: message });
      });

    return () => controller.abort();
  }, []);

  return state;
}
