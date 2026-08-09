import type {
  MapDefinition,
  MapLevelDefinition,
  PlayerSnapshot,
  Team,
  Vector3,
} from '../types/protocol';

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

export interface StableMapLevelSelection {
  level: MapLevelDefinition | undefined;
  confidence: number;
  retained: boolean;
}

export function selectStableMapLevel(
  levels: readonly MapLevelDefinition[] | undefined,
  z: number,
  previousLevelId?: string,
  hysteresis = 24,
): StableMapLevelSelection {
  const selected = selectMapLevel(levels, z);
  if (!selected || !levels?.length || !Number.isFinite(hysteresis) || hysteresis <= 0) {
    return { level: selected, confidence: 0, retained: false };
  }

  const previous = previousLevelId
    ? levels.find((level) => level.id === previousLevelId)
    : undefined;
  const retainPrevious = previous && previous.id !== selected.id
    && z >= previous.minZ - hysteresis
    && z < previous.maxZ + hysteresis;
  const level = retainPrevious ? previous : selected;
  const boundaryDistance = Math.min(
    Math.abs(z - level.minZ),
    Math.abs(level.maxZ - z),
  );
  return {
    level,
    confidence: Math.min(1, Math.max(0, boundaryDistance / hysteresis)),
    retained: Boolean(retainPrevious),
  };
}

type FloorReferencePlayer = Pick<
  PlayerSnapshot,
  'id' | 'team' | 'alive' | 'position'
>;

export function selectReferenceZ(
  players: readonly FloorReferencePlayer[],
  localPlayerId?: string | null,
  observedPlayerId?: string | null,
  localTeam?: Team | null,
): number | undefined {
  const validZ = (player: FloorReferencePlayer | undefined): number | undefined => {
    const position = player?.position;
    return position &&
      Number.isFinite(position.x) &&
      Number.isFinite(position.y) &&
      Number.isFinite(position.z)
      ? position.z
      : undefined;
  };
  const localPlayer = players.find((player) => player.id === localPlayerId);
  const observedPlayer = players.find((player) => player.id === observedPlayerId);

  if (localPlayer?.alive) {
    const z = validZ(localPlayer);
    if (z !== undefined) return z;
  }
  if (observedPlayer?.alive) {
    const observedZ = validZ(observedPlayer);
    if (observedZ !== undefined) return observedZ;
  }

  const isPlayingTeam = (team: Team | null | undefined): team is 'T' | 'CT' =>
    team === 'T' || team === 'CT';
  const referenceTeam = isPlayingTeam(localPlayer?.team)
    ? localPlayer.team
    : isPlayingTeam(localTeam)
      ? localTeam
      : undefined;
  if (referenceTeam) {
    for (const player of players) {
      if (!player.alive || player.team !== referenceTeam) continue;
      const z = validZ(player);
      if (z !== undefined) return z;
    }
  }

  for (const player of players) {
    if (!player.alive) continue;
    const z = validZ(player);
    if (z !== undefined) return z;
  }

  // With nobody alive, retain the local player's last valid floor before
  // falling back to any positioned player.
  const localZ = validZ(localPlayer);
  if (localZ !== undefined) return localZ;
  for (const player of players) {
    const z = validZ(player);
    if (z !== undefined) return z;
  }
  return undefined;
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
