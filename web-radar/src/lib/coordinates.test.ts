import { describe, expect, it } from 'vitest';
import {
  cssHeadingFromGameYaw,
  projectWorldPoint,
  resolveMapImage,
  selectMapLevel,
  selectStableMapLevel,
  selectReferenceZ,
  unwrapHeading,
} from './coordinates';
import type { MapDefinition } from '../types/protocol';

const map: MapDefinition = {
  id: 'de_example',
  name: 'Example',
  origin: { x: -3230, y: 1713 },
  scale: 5,
  image: '/maps/de_example/main.png',
  levels: [
    { id: 'lower', image: '/maps/de_example/lower.png', minZ: -1000, maxZ: 0 },
    { id: 'upper', image: '/maps/de_example/upper.png', minZ: 0.01, maxZ: 1000 },
  ],
};

describe('projectWorldPoint', () => {
  it('maps the world origin to the upper-left texture corner', () => {
    expect(projectWorldPoint({ x: -3230, y: 1713, z: 0 }, map)).toEqual({
      x: 0,
      y: 0,
      inBounds: true,
    });
  });

  it('normalizes coordinates and flips the world Y axis', () => {
    const point = projectWorldPoint({ x: -670, y: -847, z: 50 }, map);
    expect(point.x).toBeCloseTo(0.5);
    expect(point.y).toBeCloseTo(0.5);
    expect(point.inBounds).toBe(true);
  });

  it('preserves out-of-range coordinates and flags them', () => {
    const point = projectWorldPoint({ x: -3231, y: 1713, z: 0 }, map);
    expect(point.x).toBeLessThan(0);
    expect(point.inBounds).toBe(false);
  });
});

describe('map resources and headings', () => {
  it('chooses the matching floor from player Z', () => {
    expect(selectMapLevel(map.levels, -5)?.id).toBe('lower');
    expect(resolveMapImage(map, 12)).toBe('/maps/de_example/upper.png');
    expect(selectMapLevel(map.levels, 5000)?.id).toBe('upper');
  });

  it('holds the previous floor near a boundary and reports confidence', () => {
    const held = selectStableMapLevel(map.levels, 5, 'lower');
    expect(held.level?.id).toBe('lower');
    expect(held.retained).toBe(true);
    expect(held.confidence).toBeLessThan(0.3);

    const switched = selectStableMapLevel(map.levels, 25, 'lower');
    expect(switched.level?.id).toBe('upper');
    expect(switched.retained).toBe(false);
    expect(switched.confidence).toBeGreaterThan(0.9);
  });

  it('uses manifest image before the conventional fallback', () => {
    expect(resolveMapImage(map)).toBe('/maps/de_example/main.png');
    expect(resolveMapImage({ ...map, image: undefined, levels: undefined })).toBe(
      '/maps/de_example/radar.png',
    );
  });

  it('uses a live teammate floor when the local player is dead', () => {
    const players = [
      { id: 'local', team: 'CT' as const, alive: false, position: { x: 0, y: 0, z: -50 } },
      { id: 'other', team: 'T' as const, alive: true, position: { x: 0, y: 0, z: -25 } },
      { id: 'observed', team: 'CT' as const, alive: true, position: { x: 0, y: 0, z: 50 } },
    ];

    expect(selectReferenceZ(players, 'local', 'observed')).toBe(50);
    expect(resolveMapImage(map, selectReferenceZ(players, 'local', 'observed'))).toBe(
      '/maps/de_example/upper.png',
    );

    players[0].alive = true;
    expect(selectReferenceZ(players, 'local', 'observed')).toBe(-50);

    players[0].alive = false;
    players[1].alive = false;
    players[2].alive = false;
    expect(selectReferenceZ(players, 'local')).toBe(-50);
  });

  it('ignores a dead observed player and prefers a live local teammate', () => {
    const players = [
      {
        id: 'local',
        team: 'CT' as const,
        alive: false,
        position: { x: 0, y: 0, z: 50 },
      },
      {
        id: 'observed',
        team: 'T' as const,
        alive: false,
        position: { x: 0, y: 0, z: 50 },
      },
      {
        id: 'live-enemy',
        team: 'T' as const,
        alive: true,
        position: { x: 0, y: 0, z: 25 },
      },
      {
        id: 'live-teammate',
        team: 'CT' as const,
        alive: true,
        position: { x: 0, y: 0, z: -50 },
      },
    ];

    const referenceZ = selectReferenceZ(
      players,
      'local',
      'observed',
      'CT',
    );
    expect(referenceZ).toBe(-50);
    expect(resolveMapImage(map, referenceZ)).toBe('/maps/de_example/lower.png');

    players[1].alive = true;
    players[1].position = { x: Number.NaN, y: 0, z: 50 };
    expect(selectReferenceZ(players, 'local', 'observed', 'CT')).toBe(-50);
  });

  it('converts counter-clockwise game yaw to clockwise CSS rotation', () => {
    expect(cssHeadingFromGameYaw(0)).toBe(0);
    expect(cssHeadingFromGameYaw(90)).toBe(270);
    expect(cssHeadingFromGameYaw(-90)).toBe(90);
    expect(cssHeadingFromGameYaw(450)).toBe(270);
    expect(unwrapHeading(358, 2)).toBe(362);
    expect(unwrapHeading(2, 358)).toBe(-2);
    expect(unwrapHeading(722, 2)).toBe(722);
  });
});
