import type { StreamStatus } from '../lib/stream';

interface StatusHeaderProps {
  status: StreamStatus;
  stale: boolean;
  mapName: string;
  sequence: number | null;
  error: string | null;
  settingsOpen: boolean;
  deploymentLabel: string;
  onToggleSettings: () => void;
  onLogout?: () => void;
  logoutPending: boolean;
}

const statusLabels: Record<StreamStatus, string> = {
  connecting: '正在连接',
  connected: '实时连接',
  reconnecting: '正在重连',
  offline: '设备离线',
  disconnected: '已断开',
};

export function StatusHeader({
  status,
  stale,
  mapName,
  sequence,
  error,
  settingsOpen,
  deploymentLabel,
  onToggleSettings,
  onLogout,
  logoutPending,
}: StatusHeaderProps) {
  const displayStatus = status === 'connected' && stale ? '数据暂停' : statusLabels[status];
  const stateClass = status === 'connected' && !stale ? 'is-live' : 'is-warn';

  return (
    <header className="status-header">
      <div className="brand-block">
        <span className="brand-mark" aria-hidden="true">
          <i />
          <i />
          <i />
        </span>
        <div>
          <strong>TACTICAL MAP</strong>
          <span>FIXED NORTH RADAR</span>
        </div>
      </div>

      <div className="match-meta" aria-label="比赛状态">
        <span className={`connection-pill ${stateClass}`} role="status" aria-live="polite">
          <i aria-hidden="true" />
          {displayStatus}
        </span>
        <span className="map-name">{mapName || '等待地图'}</span>
        {sequence !== null && <span className="sequence">#{sequence}</span>}
      </div>

      <div className="header-actions">
        {error && <span className="compact-error" title={error} aria-label={`连接异常：${error}`}>连接异常</span>}
        <span className="deployment-label" title={deploymentLabel}>{deploymentLabel}</span>
        <button
          type="button"
          className={`icon-button ${settingsOpen ? 'is-active' : ''}`}
          onClick={onToggleSettings}
          aria-label="Radar 设置"
          aria-expanded={settingsOpen}
        >
          <span aria-hidden="true">☷</span>
          <span>设置</span>
        </button>
        {onLogout && (
          <button
            type="button"
            className="icon-button logout-button"
            onClick={onLogout}
            disabled={logoutPending}
            aria-label={logoutPending ? '正在退出 Radar Relay' : '退出 Radar Relay 会话'}
          >
            {logoutPending ? '退出中…' : '退出'}
          </button>
        )}
      </div>
    </header>
  );
}
