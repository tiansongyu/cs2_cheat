import type { MapDefinition, MapLevelDefinition, Vector3 } from '../types/protocol';

export interface RadarPoint {
  x: number;
  y: number;
  inBounds: boolean;
}

export function projectWorldPoint(
  point: Vector3,
  map: Pick<MapDefinition, 'origin' | 'scale'>,
  textureSize = 1024,
): RadarPoint {
  if (!Number.isFinite(textureSize) || textureSize <= 0 || map.scale <= 0) {
    return { x: 0, y: 0, inBounds: false };
  }

  const denominator = map.scale * textureSize;
  const x = (point.x - map.origin.x) / denominator;
  const y = (map.origin.y - point.y) / denominator;
  return {
    x,
    y,
    inBounds: x >= 0 && x <= 1 && y >= 0 && y <= 1,
  };
}

export function selectMapLevel(
  levels: readonly MapLevelDefinition[] | undefined,
  z: number,
): MapLevelDefinition | undefined {
  if (!levels?.length || !Number.isFinite(z)) return undefined;
  const valid = levels.filter(
    (level) =>
      Number.isFinite(level.minZ) &&
      Number.isFinite(level.maxZ) &&
      level.minZ < level.maxZ,
  );
  const containing = valid.find(
    (level) => z >= level.minZ && z < level.maxZ,
  );
  if (containing) return containing;

  return valid.reduce<MapLevelDefinition | undefined>((nearest, level) => {
    if (!nearest) return level;
    const distance = z < level.minZ ? level.minZ - z : z - level.maxZ;
    const nearestDistance =
      z < nearest.minZ ? nearest.minZ - z : z - nearest.maxZ;
    return distance < nearestDistance ? level : nearest;
  }, undefined);
}

export function resolveMapImage(map: MapDefinition, z?: number): string {
  const level = z === undefined ? undefined : selectMapLevel(map.levels, z);
  if (level?.image) return level.image;
  if (map.image) return map.image;
  return `/maps/${encodeURIComponent(map.id)}/radar.png`;
}

export function cssHeadingFromGameYaw(yaw: number): number {
  if (!Number.isFinite(yaw)) return 0;
  return ((-yaw % 360) + 360) % 360;
}

export function unwrapHeading(previous: number | undefined, wrapped: number): number {
  if (!Number.isFinite(wrapped)) return previous ?? 0;
  if (previous === undefined || !Number.isFinite(previous)) return wrapped;
  const shortestDelta = (((wrapped - previous + 540) % 360) + 360) % 360 - 180;
  return previous + shortestDelta;
}
