import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import {
  act,
  create,
  type ReactTestRenderer,
} from 'react-test-renderer';
import { describe, expect, it, vi } from 'vitest';
import { cssHeadingFromGameYaw } from '../lib/coordinates';
import type { MapDefinition, PlayerSnapshot } from '../types/protocol';
import { RadarMap } from './RadarMap';

(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean })
  .IS_REACT_ACT_ENVIRONMENT = true;

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

function radarElement(
  players: PlayerSnapshot[],
  radarMap: MapDefinition = map,
  observedPlayerId?: string | null,
) {
  return createElement(RadarMap, {
    mapId: radarMap.id,
    map: radarMap,
    players,
    localPlayerId: 'local',
    observedPlayerId,
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
  });
}

function renderPlayers(
  players: PlayerSnapshot[],
  radarMap: MapDefinition = map,
  observedPlayerId?: string | null,
): string {
  return renderToStaticMarkup(radarElement(
    players,
    radarMap,
    observedPlayerId,
  ));
}

function markerNode(renderer: ReactTestRenderer) {
  const markers = renderer.root.findAll((node) =>
    node.type === 'div' &&
    typeof node.props.className === 'string' &&
    node.props.className.startsWith('map-player '));
  expect(markers).toHaveLength(1);
  return markers[0];
}

function markerHeading(renderer: ReactTestRenderer): string | undefined {
  return markerNode(renderer).props.style['--heading'] as string | undefined;
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

  it('switches the background to a live teammate floor when local is dead', () => {
    const layeredMap: MapDefinition = {
      ...map,
      levels: [
        { id: 'lower', minZ: -100, maxZ: 0, image: '/maps/de_test/lower.png' },
        { id: 'upper', minZ: 0, maxZ: 100, image: '/maps/de_test/upper.png' },
      ],
    };
    const html = renderPlayers([
      player({
        id: 'local',
        alive: false,
        position: { x: 512, y: 512, z: -50 },
      }),
      player({
        id: 'other-alive',
        alive: true,
        position: { x: 516, y: 516, z: -25 },
      }),
      player({
        id: 'observed',
        alive: true,
        position: { x: 520, y: 520, z: 50 },
      }),
    ], layeredMap, 'observed');

    expect(html).toContain('src="/maps/de_test/upper.png"');
    expect(html).toContain('<small>upper</small>');
    expect(html).toContain('floor-mark is-lower');
  });

  it('ignores a dead observed floor and follows a live local teammate', () => {
    const layeredMap: MapDefinition = {
      ...map,
      levels: [
        { id: 'lower', minZ: -100, maxZ: 0, image: '/maps/de_test/lower.png' },
        { id: 'upper', minZ: 0, maxZ: 100, image: '/maps/de_test/upper.png' },
      ],
    };
    const html = renderPlayers([
      player({
        id: 'local',
        team: 'CT',
        alive: false,
        position: { x: 500, y: 500, z: 50 },
      }),
      player({
        id: 'observed',
        team: 'T',
        alive: false,
        position: { x: 510, y: 510, z: 50 },
      }),
      player({
        id: 'live-enemy',
        team: 'T',
        alive: true,
        position: { x: 520, y: 520, z: 25 },
      }),
      player({
        id: 'live-teammate',
        name: 'Live Teammate',
        team: 'CT',
        alive: true,
        position: { x: 530, y: 530, z: -50 },
      }),
    ], layeredMap, 'observed');

    expect(html).toContain('src="/maps/de_test/lower.png"');
    expect(html).toContain('<small>lower</small>');
    expect(html).toContain('Live Teammate');
  });

  it('renders exactly one correctly directed marker for each of 64 mixed players', () => {
    const players = Array.from({ length: 64 }, (_, index) => {
      const alive = index % 4 !== 0;
      return player({
        id: `player-${index}`,
        name: `P-${index.toString().padStart(2, '0')}`,
        team: index % 2 === 0 ? 'CT' : 'T',
        alive,
        dormant: index % 7 === 0,
        position: {
          x: 64 + (index % 8) * 112,
          y: 960 - Math.floor(index / 8) * 112,
          z: index % 3 === 0 ? -50 : 50,
        },
        yaw: alive ? index * 37 - 720 : null,
        health: alive ? 100 : 0,
      });
    });

    const html = renderPlayers(players);
    const markerTags = html.match(/<div class="map-player[^>]*>/g) ?? [];
    const alivePlayers = players.filter((entry) => entry.alive);
    const deadPlayers = players.filter((entry) => !entry.alive);
    const dormantPlayers = players.filter((entry) => entry.dormant);

    expect(markerTags).toHaveLength(64);
    expect(markerTags.filter((tag) => tag.includes('team-ct'))).toHaveLength(32);
    expect(markerTags.filter((tag) => tag.includes('team-t '))).toHaveLength(32);
    expect(markerTags.filter((tag) => tag.includes('is-alive'))).toHaveLength(alivePlayers.length);
    expect(markerTags.filter((tag) => tag.includes('is-dead'))).toHaveLength(deadPlayers.length);
    expect(markerTags.filter((tag) => tag.includes('is-dormant'))).toHaveLength(dormantPlayers.length);
    expect(html.match(/class="direction-arrow /g)).toHaveLength(alivePlayers.length);
    expect(html.match(/class="death-mark"/g)).toHaveLength(deadPlayers.length);
    expect(html).not.toContain('has-no-heading');

    for (const entry of players) {
      const title = `${entry.name} · ${entry.health} HP`;
      const matchingMarkers = markerTags.filter((tag) =>
        tag.includes(`title="${title}"`));
      expect(matchingMarkers).toHaveLength(1);
      if (entry.alive && entry.yaw !== null) {
        expect(matchingMarkers[0]).toContain(
          `--heading:${cssHeadingFromGameYaw(entry.yaw)}deg`,
        );
      }
    }
  });

  it('does not reuse heading history after death, respawn, or disappearance', () => {
    const cachePlayer = (yaw: number, alive = true) => player({
      id: 'cache-player',
      name: 'Cache Player',
      alive,
      health: alive ? 100 : 0,
      yaw,
    });
    let renderer!: ReactTestRenderer;

    act(() => {
      renderer = create(radarElement([cachePlayer(340)]));
    });
    // Commit the initial heading, then verify wrap-around remains visually
    // continuous on the following frames.
    act(() => {
      renderer.update(radarElement([cachePlayer(350)]));
    });
    expect(markerHeading(renderer)).toBe('10deg');
    act(() => {
      renderer.update(radarElement([cachePlayer(10)]));
    });
    expect(markerHeading(renderer)).toBe('-10deg');

    act(() => {
      renderer.update(radarElement([cachePlayer(180, false)]));
    });
    expect(renderer.root.findAllByProps({ className: 'death-mark' })).toHaveLength(1);
    act(() => {
      renderer.update(radarElement([cachePlayer(10)]));
    });
    expect(markerHeading(renderer)).toBe('350deg');

    act(() => {
      renderer.update(radarElement([]));
    });
    act(() => {
      renderer.update(radarElement([cachePlayer(350)]));
    });
    expect(markerHeading(renderer)).toBe('10deg');

    act(() => renderer.unmount());
  });

  it('resets accumulated heading history immediately when the map changes', () => {
    const cachePlayer = (yaw: number) => player({
      id: 'map-heading-player',
      name: 'Map Heading Player',
      yaw,
    });
    const otherMap: MapDefinition = {
      ...map,
      id: 'de_other',
      name: 'Other',
    };
    let renderer!: ReactTestRenderer;

    act(() => {
      renderer = create(radarElement([cachePlayer(10)]));
    });
    expect(markerHeading(renderer)).toBe('350deg');
    act(() => {
      renderer.update(radarElement([cachePlayer(-10)]));
    });
    expect(markerHeading(renderer)).toBe('370deg');
    const oldMapMarker = markerNode(renderer);

    act(() => {
      renderer.update(radarElement([cachePlayer(-10)], otherMap));
    });
    expect(markerHeading(renderer)).toBe('10deg');
    expect(markerNode(renderer)).not.toBe(oldMapMarker);

    act(() => renderer.unmount());
  });
});
