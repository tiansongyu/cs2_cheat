import type { RadarSettings } from '../hooks/useRadarSettings';
import type { SnapshotReplayState } from '../hooks/useSnapshotReplay';

interface SettingsPanelProps {
  settings: RadarSettings;
  onChange: (settings: RadarSettings) => void;
  onClose: () => void;
  replay: SnapshotReplayState;
}

export function SettingsPanel({ settings, onChange, onClose, replay }: SettingsPanelProps) {
  const update = <K extends keyof RadarSettings>(key: K, value: RadarSettings[K]) => {
    onChange({ ...settings, [key]: value });
  };

  return (
    <aside className="settings-panel" aria-label="Radar 设置">
      <div className="settings-heading">
        <div>
          <strong>显示设置</strong>
          <span>仅保存在当前浏览器</span>
        </div>
        <button type="button" onClick={onClose} aria-label="关闭设置">×</button>
      </div>

      <label className="range-setting">
        <span>
          玩家图标
          <output>{settings.playerSize}px</output>
        </span>
        <input
          type="range"
          min="12"
          max="34"
          value={settings.playerSize}
          onChange={(event) => update('playerSize', Number(event.target.value))}
        />
      </label>

      <label className="range-setting">
        <span>
          C4 图标
          <output>{settings.bombSize}px</output>
        </span>
        <input
          type="range"
          min="18"
          max="42"
          value={settings.bombSize}
          onChange={(event) => update('bombSize', Number(event.target.value))}
        />
      </label>

      <label className="toggle-setting">
        <span>地图显示姓名</span>
        <input
          type="checkbox"
          checked={settings.showNames}
          onChange={(event) => update('showNames', event.target.checked)}
        />
        <i aria-hidden="true" />
      </label>

      <label className="toggle-setting">
        <span>面板显示装备</span>
        <input
          type="checkbox"
          checked={settings.showEquipment}
          onChange={(event) => update('showEquipment', event.target.checked)}
        />
        <i aria-hidden="true" />
      </label>

      <div className="replay-setting">
        <strong>快照回放</strong>
        <label className="replay-file-button">
          载入 NDJSON
          <input
            type="file"
            accept=".ndjson,application/x-ndjson,application/json"
            onChange={(event) => {
              const file = event.target.files?.[0];
              if (file) void replay.load(file);
              event.target.value = '';
            }}
          />
        </label>
        {replay.active && (
          <>
            <span>{replay.fileName} · {replay.current}/{replay.total}</span>
            <input
              type="range"
              min="1"
              max={replay.total}
              value={replay.current}
              onChange={(event) => replay.seek(Number(event.target.value) - 1)}
            />
            <div className="replay-actions">
              <button type="button" onClick={replay.togglePlaying}>
                {replay.playing ? '暂停' : '播放'}
              </button>
              <button type="button" onClick={replay.stop}>停止回放</button>
            </div>
          </>
        )}
        {replay.error && <span className="settings-error">{replay.error}</span>}
      </div>
    </aside>
  );
}
