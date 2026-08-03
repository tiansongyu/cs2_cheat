import { useCallback, useState } from 'react';

export interface RadarSettings {
  playerSize: number;
  bombSize: number;
  showNames: boolean;
  showEquipment: boolean;
}

const STORAGE_KEY = 'cs2.webRadar.settings.v1';
const defaults: RadarSettings = {
  playerSize: 20,
  bombSize: 28,
  showNames: true,
  showEquipment: true,
};

function clamp(value: unknown, min: number, max: number, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value)
    ? Math.min(max, Math.max(min, value))
    : fallback;
}

export function loadRadarSettings(storage: Pick<Storage, 'getItem'>): RadarSettings {
  try {
    const raw = storage.getItem(STORAGE_KEY);
    if (!raw) return defaults;
    const value = JSON.parse(raw) as Partial<RadarSettings>;
    return {
      playerSize: clamp(value.playerSize, 12, 34, defaults.playerSize),
      bombSize: clamp(value.bombSize, 18, 42, defaults.bombSize),
      showNames: typeof value.showNames === 'boolean' ? value.showNames : defaults.showNames,
      showEquipment:
        typeof value.showEquipment === 'boolean' ? value.showEquipment : defaults.showEquipment,
    };
  } catch {
    return defaults;
  }
}

export function useRadarSettings() {
  const [settings, setSettingsState] = useState<RadarSettings>(() =>
    loadRadarSettings(window.localStorage),
  );

  const setSettings = useCallback((next: RadarSettings) => {
    setSettingsState(next);
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
    } catch {
      // Radar remains usable when storage is disabled.
    }
  }, []);

  return { settings, setSettings };
}
