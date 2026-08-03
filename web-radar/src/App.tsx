import { useState } from 'react';
import { PlayerPanel } from './components/PlayerPanel';
import { RadarMap } from './components/RadarMap';
import { SettingsPanel } from './components/SettingsPanel';
import { StatusHeader } from './components/StatusHeader';
import { useAnimationClock } from './hooks/useAnimationClock';
import { useMapManifest } from './hooks/useMapManifest';
import { useRadarSettings } from './hooks/useRadarSettings';
import { useRadarStream } from './hooks/useRadarStream';

export default function App() {
  const stream = useRadarStream();
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
        onToggleSettings={() => setSettingsOpen((value) => !value)}
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
          manifestError={maps.error}
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
        <span>协议 v1</span>
        <span>{players.length} 名玩家</span>
      </footer>
    </div>
  );
}
