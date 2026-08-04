import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it, vi } from 'vitest';
import type { MapDefinition, PlayerSnapshot } from '../types/protocol';
import { RadarMap } from './RadarMap';

const map: MapDefinition = {
  id: 'de_test',
  name: 'Test',
  origin: { x: 0, y: 1024 },
  scale: 1,
  image: '/maps/de_test/radar.png',
};

function player(overrides: Partial<PlayerSnapshot>): PlayerSnapshot {
  return {
    id: 'player',
    name: 'Player',
    team: 'CT',
    alive: true,
    position: { x: 512, y: 512, z: 0 },
    yaw: 90,
    health: 100,
    armor: 0,
    money: 0,
    ...overrides,
  };
}

function renderPlayers(
  players: PlayerSnapshot[],
  radarMap: MapDefinition = map,
): string {
  return renderToStaticMarkup(createElement(RadarMap, {
    mapId: radarMap.id,
    map: radarMap,
    players,
    localPlayerId: 'local',
    bomb: { state: 'unknown' },
    capturedAtMs: 1,
    receivedAtPerformanceMs: 1,
    performanceNowMs: 1,
    settings: {
      playerSize: 20,
      bombSize: 28,
      showNames: true,
      showEquipment: true,
    },
    stale: false,
    manifestError: null,
    manifestLoading: false,
    onRetryMap: vi.fn(),
  }));
}

describe('RadarMap player markers', () => {
  it('renders every positioned team player with a fixed-map heading', () => {
    const html = renderPlayers([
      player({ id: 'local', name: 'Local', team: 'CT', yaw: 0 }),
      player({ id: 'enemy', name: 'Enemy', team: 'T', yaw: 180 }),
    ]);

    expect(html.match(/class="map-player/g)).toHaveLength(2);
    expect(html).toContain('Local');
    expect(html).toContain('Enemy');
    expect(html).toContain('--heading:0deg');
    expect(html).toContain('--heading:180deg');
  });

  it('does not invent an east heading and rejects invalid competitive colours', () => {
    const html = renderPlayers([
      player({ competitiveColor: -1, yaw: null }),
    ]);

    expect(html).toContain('direction-arrow has-no-heading');
    expect(html).toContain('--player-accent:#56c7f2');
  });

  it('marks a player at maxZ as belonging to the upper floor', () => {
    const layeredMap: MapDefinition = {
      ...map,
      levels: [
        { id: 'lower', minZ: -100, maxZ: 0, image: '/maps/de_test/radar_lower.png' },
        { id: 'upper', minZ: 0, maxZ: 100, image: '/maps/de_test/radar.png' },
      ],
    };
    const html = renderPlayers([
      player({ id: 'local', position: { x: 512, y: 512, z: -50 } }),
      player({ id: 'boundary', position: { x: 520, y: 520, z: 0 } }),
    ], layeredMap);

    expect(html).toContain('floor-mark is-upper');
  });
});
