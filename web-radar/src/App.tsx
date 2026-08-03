import { useEffect, useMemo, useState } from 'react';
import { ConnectionNotice } from './components/ConnectionNotice';
import { PlayerPanel } from './components/PlayerPanel';
import { RadarMap } from './components/RadarMap';
import { RelayAccessGate } from './components/RelayAccessGate';
import { SettingsPanel } from './components/SettingsPanel';
import { StatusHeader } from './components/StatusHeader';
import { useAnimationClock } from './hooks/useAnimationClock';
import { useMapManifest } from './hooks/useMapManifest';
import { useRadarSettings } from './hooks/useRadarSettings';
import { useRadarStream } from './hooks/useRadarStream';
import { useRelaySession } from './hooks/useRelaySession';
import {
  resolveRadarDeployment,
  sanitizeRelayUrl,
  type RadarDeploymentMode,
} from './lib/deployment';

interface RadarWorkspaceProps {
  mode: RadarDeploymentMode;
  room: string | null;
  onSessionRejected: () => void;
  onLogout?: () => void;
  logoutPending: boolean;
}

function RadarWorkspace({
  mode,
  room,
  onSessionRejected,
  onLogout,
  logoutPending,
}: RadarWorkspaceProps) {
  const stream = useRadarStream({ mode, onSessionRejected });
  const maps = useMapManifest();
  const { settings, setSettings } = useRadarSettings();
  const [settingsOpen, setSettingsOpen] = useState(false);
  const frame = stream.frame;
  const snapshot = frame?.snapshot;
  const mapId = snapshot?.map.id ?? '';
  const map = maps.manifest?.maps.find((entry) => entry.id === mapId);
  const performanceNowMs = useAnimationClock(snapshot?.bomb.state === 'planted' && !stream.stale);
  const players = snapshot?.players ?? [];

  return (
    <div className="app-shell">
      <StatusHeader
        status={stream.status}
        stale={stream.stale}
        mapName={map?.name ?? snapshot?.map.displayName ?? mapId}
        sequence={snapshot?.seq ?? null}
        error={stream.error}
        settingsOpen={settingsOpen}
        deploymentLabel={mode === 'relay' ? `RELAY · ${room ?? ''}` : 'LOCAL'}
        onToggleSettings={() => setSettingsOpen((value) => !value)}
        onLogout={onLogout}
        logoutPending={logoutPending}
      />

      <ConnectionNotice
        status={stream.status}
        stale={stream.stale}
        error={stream.error}
        retryInMs={stream.retryInMs}
        lastReceivedAtMs={frame?.receivedAtWallMs ?? null}
        hasFrame={frame !== null}
        onRetry={stream.retry}
      />

      {settingsOpen && (
        <SettingsPanel
          settings={settings}
          onChange={setSettings}
          onClose={() => setSettingsOpen(false)}
        />
      )}

      <main className="radar-layout">
        <PlayerPanel
          team="CT"
          players={players}
          localPlayerId={snapshot?.localPlayerId}
          showEquipment={settings.showEquipment}
        />
        <RadarMap
          mapId={mapId}
          map={map}
          players={players}
          localPlayerId={snapshot?.localPlayerId}
          bomb={snapshot?.bomb ?? { state: 'unknown' }}
          capturedAtMs={snapshot?.capturedAtMs ?? null}
          receivedAtPerformanceMs={frame?.receivedAtPerformanceMs ?? null}
          performanceNowMs={performanceNowMs}
          settings={settings}
          stale={stream.stale}
          staleMessage={stream.status === 'offline' ? '设备离线，等待网络恢复' : undefined}
          manifestError={maps.error}
          manifestLoading={maps.loading}
          onRetryMap={maps.retry}
        />
        <PlayerPanel
          team="T"
          players={players}
          localPlayerId={snapshot?.localPlayerId}
          showEquipment={settings.showEquipment}
        />
      </main>

      <footer className="app-footer">
        <span><i className="legend-dot ct" /> CT</span>
        <span><i className="legend-dot t" /> T</span>
        <span><i className="legend-cross">×</i> 阵亡</span>
        <span className="footer-spacer" />
        <span>{mode === 'relay' ? '安全 Relay' : '内嵌服务'}</span>
        <span>协议 v1</span>
        <span>{players.length} 名玩家</span>
      </footer>
    </div>
  );
}

export default function App() {
  const mode = useMemo(() => resolveRadarDeployment(window.location), []);
  const relay = useRelaySession(mode);

  useEffect(() => {
    if (mode !== 'relay') return;
    const sanitized = sanitizeRelayUrl(window.location);
    if (sanitized !== null) window.history.replaceState(window.history.state, '', sanitized);
  }, [mode]);

  if (mode === 'relay' && relay.access.status !== 'authenticated') {
    return (
      <RelayAccessGate
        access={relay.access}
        submitting={relay.submitting}
        actionError={relay.actionError}
        onLogin={relay.login}
        onRetry={relay.retry}
      />
    );
  }

  const room = relay.access.status === 'authenticated' ? relay.access.session.room : null;
  return (
    <RadarWorkspace
      mode={mode}
      room={room}
      onSessionRejected={relay.invalidate}
      onLogout={mode === 'relay' ? () => void relay.logout() : undefined}
      logoutPending={relay.submitting}
    />
  );
}
