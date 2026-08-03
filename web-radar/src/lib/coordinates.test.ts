import { describe, expect, it } from 'vitest';
import {
  cssHeadingFromGameYaw,
  projectWorldPoint,
  resolveMapImage,
  selectMapLevel,
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

  it('uses manifest image before the conventional fallback', () => {
    expect(resolveMapImage(map)).toBe('/maps/de_example/main.png');
    expect(resolveMapImage({ ...map, image: undefined, levels: undefined })).toBe(
      '/maps/de_example/radar.png',
    );
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
