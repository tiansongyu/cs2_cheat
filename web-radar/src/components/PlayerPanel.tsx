import { compactWeaponName, teamLabel } from '../lib/format';
import type { PlayerSnapshot, Team } from '../types/protocol';

interface PlayerPanelProps {
  team: Extract<Team, 'T' | 'CT'>;
  players: PlayerSnapshot[];
  localPlayerId?: string | null;
  showEquipment: boolean;
}

function PlayerCard({
  player,
  isLocal,
  showEquipment,
}: {
  player: PlayerSnapshot;
  isLocal: boolean;
  showEquipment: boolean;
}) {
  const weapons = player.inventory ?? player.weapons ?? [];
  const active = player.activeWeapon ?? weapons.find((weapon) => weapon.active);

  return (
    <article className={`player-card ${player.alive ? '' : 'is-dead'} ${isLocal ? 'is-local' : ''}`}>
      <div className="player-card-topline">
        {player.steamId ? (
          <a
            className="player-name"
            href={`https://steamcommunity.com/profiles/${encodeURIComponent(player.steamId)}`}
            target="_blank"
            rel="noopener noreferrer"
            title="打开 Steam 社区资料"
          >
            {isLocal && <i className="local-dot" aria-label="本地玩家" />}
            {player.name || 'ANONYMOUS'}
          </a>
        ) : (
          <span className="player-name" title={player.name ?? 'ANONYMOUS'}>
            {isLocal && <i className="local-dot" aria-label="本地玩家" />}
            {player.name || 'ANONYMOUS'}
          </span>
        )}
        <span className="player-money">${Math.max(0, player.money).toLocaleString('en-US')}</span>
      </div>

      <div className="vitals-row">
        <span className="health-stat">{Math.max(0, player.health)}</span>
        <span className="health-track" aria-label={`生命值 ${player.health}`}>
          <i style={{ width: `${Math.min(100, Math.max(0, player.health))}%` }} />
        </span>
        <span className="armor-stat">◇ {Math.max(0, player.armor)}</span>
      </div>

      <div className="kit-row">
        {player.hasHelmet && <span title="头盔">H</span>}
        {player.hasDefuser && <span title="拆弹器">K</span>}
        {player.hasBomb && <span className="bomb-chip" title="携带 C4">C4</span>}
        {!player.alive && <span className="dead-chip">阵亡</span>}
        {active && <strong title="当前武器">{compactWeaponName(active)}</strong>}
      </div>

      {showEquipment && weapons.length > 0 && (
        <div className="equipment-row" aria-label="装备">
          {weapons.map((weapon, index) => (
            <span
              className={
                weapon.active ||
                (active &&
                  ((weapon.definitionIndex !== undefined &&
                    weapon.definitionIndex === active.definitionIndex) ||
                    weapon.name === active.name))
                  ? 'is-active'
                  : ''
              }
              key={`${weapon.name}-${index}`}
            >
              {compactWeaponName(weapon)}
            </span>
          ))}
        </div>
      )}
    </article>
  );
}

export function PlayerPanel({
  team,
  players,
  localPlayerId,
  showEquipment,
}: PlayerPanelProps) {
  const teamPlayers = players
    .filter((player) => player.team === team)
    .sort(
      (a, b) =>
        Number(b.alive) - Number(a.alive) ||
        (a.name ?? '').localeCompare(b.name ?? ''),
    );
  const alive = teamPlayers.filter((player) => player.alive).length;

  return (
    <section className={`team-panel team-${team.toLowerCase()}`} aria-label={teamLabel(team)}>
      <header>
        <div>
          <span>{team}</span>
          <strong>{teamLabel(team)}</strong>
        </div>
        <output>{alive}/{teamPlayers.length}</output>
      </header>
      <div className="player-list">
        {teamPlayers.length === 0 ? (
          <p className="panel-empty">等待玩家数据</p>
        ) : (
          teamPlayers.map((player) => (
            <PlayerCard
              key={player.id}
              player={player}
              isLocal={player.id === localPlayerId}
              showEquipment={showEquipment}
            />
          ))
        )}
      </div>
    </section>
  );
}
