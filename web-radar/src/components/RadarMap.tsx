import { useEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import type { RadarSettings } from '../hooks/useRadarSettings';
import { formatCountdown, getBombTiming, estimateHostNowMs } from '../lib/bomb';
import {
  cssHeadingFromGameYaw,
  projectWorldPoint,
  resolveMapImage,
  selectMapLevel,
  unwrapHeading,
} from '../lib/coordinates';
import { bombStateLabel } from '../lib/format';
import type { BombSnapshot, MapDefinition, PlayerSnapshot } from '../types/protocol';

interface RadarMapProps {
  mapId: string;
  map: MapDefinition | undefined;
  players: PlayerSnapshot[];
  localPlayerId?: string | null;
  bomb: BombSnapshot;
  capturedAtMs: number | null;
  receivedAtPerformanceMs: number | null;
  performanceNowMs: number;
  settings: RadarSettings;
  stale: boolean;
  staleMessage?: string;
  manifestError: string | null;
  manifestLoading: boolean;
  onRetryMap: () => void;
}

const competitivePalette = ['#9aa4b2', '#5ab4ff', '#b88cff', '#72d69d', '#ffd166', '#ff8c69'];

function playerAccent(player: PlayerSnapshot): string {
  if (
    Number.isInteger(player.competitiveColor) &&
    player.competitiveColor !== undefined &&
    player.competitiveColor >= 0 &&
    player.competitiveColor < competitivePalette.length
  ) {
    return competitivePalette[player.competitiveColor];
  }
  return player.team === 'CT' ? '#56c7f2' : player.team === 'T' ? '#f1b95b' : '#a1a8af';
}

export function RadarMap({
  mapId,
  map,
  players,
  localPlayerId,
  bomb,
  capturedAtMs,
  receivedAtPerformanceMs,
  performanceNowMs,
  settings,
  stale,
  staleMessage,
  manifestError,
  manifestLoading,
  onRetryMap,
}: RadarMapProps) {
  const [imageFailed, setImageFailed] = useState(false);
  const [imageRevision, setImageRevision] = useState(0);
  const playerHeadings = useRef(new Map<string, number>());
  const localPlayer = players.find((player) => player.id === localPlayerId);
  const referenceZ =
    localPlayer?.position?.z ?? players.find((player) => player.alive && player.position)?.position?.z;
  const imageUrl = map ? resolveMapImage(map, referenceZ) : '';
  const imageSrc = imageRevision === 0
    ? imageUrl
    : `${imageUrl}${imageUrl.includes('?') ? '&' : '?'}radar_retry=${imageRevision}`;
  const level = map && referenceZ !== undefined ? selectMapLevel(map.levels, referenceZ) : undefined;

  useEffect(() => {
    setImageFailed(false);
    setImageRevision(0);
  }, [imageUrl]);

  useEffect(() => {
    const activePlayers = new Set(players.map((player) => player.id));
    for (const playerId of playerHeadings.current.keys()) {
      if (!activePlayers.has(playerId)) playerHeadings.current.delete(playerId);
    }
  }, [players]);

  useEffect(() => {
    playerHeadings.current.clear();
  }, [mapId]);

  useEffect(() => {
    if (!imageFailed) return undefined;
    const resume = () => {
      if (document.visibilityState === 'hidden' || navigator.onLine === false) return;
      setImageFailed(false);
      setImageRevision((revision) => revision + 1);
    };
    window.addEventListener('online', resume);
    window.addEventListener('pageshow', resume);
    document.addEventListener('visibilitychange', resume);
    return () => {
      window.removeEventListener('online', resume);
      window.removeEventListener('pageshow', resume);
      document.removeEventListener('visibilitychange', resume);
    };
  }, [imageFailed]);

  const retryMapImage = () => {
    setImageFailed(false);
    setImageRevision((revision) => revision + 1);
  };

  const bombPosition = useMemo(() => {
    if (bomb.position) return bomb.position;
    const carrierId = bomb.carrierPlayerId ?? bomb.carrierId;
    if (carrierId) return players.find((player) => player.id === carrierId)?.position ?? undefined;
    return players.find((player) => player.hasBomb)?.position;
  }, [bomb.carrierId, bomb.carrierPlayerId, bomb.position, players]);

  const hostNowMs =
    capturedAtMs !== null && receivedAtPerformanceMs !== null
      ? estimateHostNowMs(
          { capturedAtMs, receivedAtPerformanceMs },
          performanceNowMs,
        )
      : 0;
  const elapsedSinceSnapshotMs =
    receivedAtPerformanceMs === null
      ? 0
      : Math.max(0, performanceNowMs - receivedAtPerformanceMs);
  const timing = getBombTiming(bomb, hostNowMs, elapsedSinceSnapshotMs);
  const visibleBomb = ['carried', 'dropped', 'planted'].includes(bomb.state);

  return (
    <section className="radar-shell" aria-label="固定北向全地图 Radar">
      <div className="radar-topbar">
        <div>
          <span className="north-indicator"><i />N</span>
          <strong>{map?.name ?? (mapId || 'NO MAP')}</strong>
          {level && <small>{level.id}</small>}
        </div>
        <span className={`bomb-status bomb-${bomb.state}`}>{bombStateLabel(bomb.state)}</span>
      </div>

      <div className="radar-frame">
        <div className="radar-surface">
          {map && !imageFailed && (
            <img
              className="radar-image"
              src={imageSrc}
              alt=""
              aria-hidden="true"
              draggable={false}
              onError={() => setImageFailed(true)}
            />
          )}

          {map &&
            players.map((player) => {
              if (!player.position) return null;
              const point = projectWorldPoint(player.position, map);
              if (!point.inBounds || player.team === 'NONE') return null;
              const floorOffset = level
                ? player.position.z < level.minZ
                  ? 'lower'
                  : player.position.z >= level.maxZ
                    ? 'upper'
                    : null
                : null;
              const heading = player.yaw === null
                ? undefined
                : unwrapHeading(
                    playerHeadings.current.get(player.id),
                    cssHeadingFromGameYaw(player.yaw),
                  );
              if (heading !== undefined) playerHeadings.current.set(player.id, heading);
              const style = {
                left: `${point.x * 100}%`,
                top: `${point.y * 100}%`,
                '--player-size': `${settings.playerSize}px`,
                '--player-accent': playerAccent(player),
                '--heading': `${heading ?? 0}deg`,
              } as CSSProperties;
              return (
                <div
                  className={`map-player team-${player.team.toLowerCase()} ${
                    player.alive ? 'is-alive' : 'is-dead'
                  } ${player.dormant ? 'is-dormant' : ''} ${
                    player.id === localPlayerId ? 'is-local' : ''
                  }`}
                  style={style}
                  key={player.id}
                  title={`${player.name || 'ANONYMOUS'} · ${player.health} HP`}
                >
                  {player.alive ? (
                    <i className={`direction-arrow ${heading === undefined ? 'has-no-heading' : ''}`} />
                  ) : <i className="death-mark">×</i>}
                  {floorOffset && (
                    <i className={`floor-mark is-${floorOffset}`}>
                      {floorOffset === 'upper' ? '↑' : '↓'}
                    </i>
                  )}
                  {player.hasBomb && <i className="carrier-mark">C4</i>}
                  {settings.showNames && <span>{player.name || 'ANONYMOUS'}</span>}
                </div>
              );
            })}

          {map && visibleBomb && bombPosition && (() => {
            const point = projectWorldPoint(bombPosition, map);
            if (!point.inBounds) return null;
            const style = {
              left: `${point.x * 100}%`,
              top: `${point.y * 100}%`,
              '--bomb-size': `${settings.bombSize}px`,
            } as CSSProperties;
            return (
              <div className={`map-bomb bomb-${bomb.state}`} style={style} title={bombStateLabel(bomb.state)}>
                C4
              </div>
            );
          })()}

          {!map && (
            <div className="map-placeholder">
              <span aria-hidden="true">⌖</span>
              <strong>
                {manifestLoading
                  ? '正在加载固定地图资源'
                  : mapId
                    ? `缺少 ${mapId} 的地图定义`
                    : '等待游戏地图'}
              </strong>
              <p>
                {manifestError
                  ? `地图清单加载失败：${manifestError}`
                  : manifestLoading
                    ? '正在读取地图清单…'
                    : mapId
                      ? '当前地图尚未收录到地图清单。'
                      : '收到游戏快照后会自动显示。'}
              </p>
              {manifestError && (
                <button type="button" onClick={onRetryMap}>重新加载地图清单</button>
              )}
            </div>
          )}

          {map && imageFailed && (
            <div className="map-placeholder image-missing">
              <span aria-hidden="true">▧</span>
              <strong>地图图像不可用</strong>
              <p>固定地图静态资源加载失败。</p>
              <button type="button" onClick={retryMapImage}>重新加载地图图像</button>
            </div>
          )}

          {stale && (
            <div className="stale-overlay" role="status" aria-live="polite">
              <strong>数据已暂停</strong>
              <span>{staleMessage ?? '正在等待新的游戏快照'}</span>
            </div>
          )}
        </div>

        {bomb.state === 'planted' && (
          <div className="bomb-timer" role="timer" aria-live="off" aria-label="炸弹倒计时">
            <span className="site-badge">
              {bomb.site && bomb.site !== 'unknown' ? bomb.site.toUpperCase() : '?'}
            </span>
            <div>
              <small>爆炸倒计时</small>
              <strong>{formatCountdown(timing.explosionRemainingMs)}s</strong>
            </div>
            {bomb.beingDefused && (
              <div className={timing.canCompleteDefuse ? 'defuse-safe' : 'defuse-danger'}>
                <small>拆除倒计时</small>
                <strong>{formatCountdown(timing.defuseRemainingMs)}s</strong>
                <span>{timing.canCompleteDefuse ? '可完成' : '来不及'}</span>
              </div>
            )}
          </div>
        )}
      </div>
    </section>
  );
}
