export const PROTOCOL_VERSION = 1 as const;

export type Team = 'T' | 'CT' | 'SPEC' | 'NONE';
export type BombState =
  | 'unknown'
  | 'carried'
  | 'dropped'
  | 'planted'
  | 'defused'
  | 'exploded';

export interface Vector3 {
  x: number;
  y: number;
  z: number;
}

export interface WeaponSnapshot {
  definitionIndex?: number;
  name: string;
  displayName?: string;
  category?:
    | 'unknown'
    | 'knife'
    | 'pistol'
    | 'smg'
    | 'rifle'
    | 'shotgun'
    | 'machineGun'
    | 'sniperRifle'
    | 'grenade'
    | 'equipment'
    | 'bomb';
  clipAmmo?: number;
  reserveAmmo?: number;
  slot?: 'primary' | 'secondary' | 'melee' | 'grenade' | 'utility' | 'unknown';
  active?: boolean;
}

export interface PlayerSnapshot {
  id: string;
  steamId?: string | null;
  name: string | null;
  team: Team;
  competitiveColor?: number;
  alive: boolean;
  dormant?: boolean;
  position: Vector3 | null;
  yaw: number | null;
  health: number;
  armor: number;
  money: number;
  hasHelmet?: boolean;
  hasDefuser?: boolean;
  hasBomb?: boolean;
  activeWeapon?: WeaponSnapshot | null;
  inventory?: WeaponSnapshot[];
  /** Transitional alias accepted from early v1 publishers. */
  weapons?: WeaponSnapshot[];
}

export interface BombSnapshot {
  state: BombState;
  position?: Vector3 | null;
  carrierPlayerId?: string | null;
  /** Transitional alias accepted from early v1 publishers. */
  carrierId?: string | null;
  site?: 'a' | 'b' | 'unknown';
  explodeInSeconds?: number | null;
  defuseInSeconds?: number | null;
  defuseWillSucceed?: boolean;
  explodeAtMs?: number | null;
  defuseEndAtMs?: number | null;
  beingDefused?: boolean;
}

export interface HelloMessage {
  v: typeof PROTOCOL_VERSION;
  type: 'hello';
  serverTimeMs: number;
  updateHz?: number;
}

export interface SnapshotMessage {
  v: typeof PROTOCOL_VERSION;
  type: 'snapshot';
  seq: number;
  capturedAtMs: number;
  map: {
    id: string;
    displayName?: string;
    phase?: string;
    roundNumber?: number;
    connected?: boolean;
  };
  localPlayerId?: string | null;
  observedPlayerId?: string | null;
  localTeam?: Team;
  players: PlayerSnapshot[];
  bomb: BombSnapshot;
}

export interface ErrorMessage {
  v: typeof PROTOCOL_VERSION;
  type: 'error';
  code: string;
  message: string;
}

export type ServerMessage = HelloMessage | SnapshotMessage | ErrorMessage;

export interface MapLevelDefinition {
  id: string;
  image: string;
  minZ: number;
  maxZ: number;
}

export interface MapDefinition {
  id: string;
  name: string;
  origin: {
    x: number;
    y: number;
  };
  scale: number;
  image?: string;
  levels?: MapLevelDefinition[];
}

export interface MapManifest {
  version: 1;
  source?: {
    project: string;
    release: string;
  };
  maps: MapDefinition[];
}

const teams = new Set<Team>(['T', 'CT', 'SPEC', 'NONE']);
const bombStates = new Set<BombState>([
  'unknown',
  'carried',
  'dropped',
  'planted',
  'defused',
  'exploded',
]);
const bombSites = new Set(['a', 'b', 'unknown']);

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value);
}

function isVector3(value: unknown): value is Vector3 {
  return (
    isRecord(value) &&
    isFiniteNumber(value.x) &&
    isFiniteNumber(value.y) &&
    isFiniteNumber(value.z)
  );
}

function isWeapon(value: unknown): value is WeaponSnapshot {
  return isRecord(value) && typeof value.name === 'string';
}

function isPlayer(value: unknown): value is PlayerSnapshot {
  if (!isRecord(value)) return false;
  return (
    typeof value.id === 'string' &&
    (value.steamId === undefined || value.steamId === null || typeof value.steamId === 'string') &&
    (typeof value.name === 'string' || value.name === null) &&
    typeof value.team === 'string' &&
    teams.has(value.team as Team) &&
    typeof value.alive === 'boolean' &&
    (isVector3(value.position) || value.position === null) &&
    (isFiniteNumber(value.yaw) || value.yaw === null) &&
    isFiniteNumber(value.health) &&
    isFiniteNumber(value.armor) &&
    isFiniteNumber(value.money) &&
    (value.activeWeapon === undefined || value.activeWeapon === null || isWeapon(value.activeWeapon)) &&
    (value.inventory === undefined ||
      (Array.isArray(value.inventory) && value.inventory.every(isWeapon))) &&
    (value.weapons === undefined || (Array.isArray(value.weapons) && value.weapons.every(isWeapon)))
  );
}

function isBomb(value: unknown): value is BombSnapshot {
  if (!isRecord(value) || typeof value.state !== 'string') return false;
  return (
    bombStates.has(value.state as BombState) &&
    (value.site === undefined || (typeof value.site === 'string' && bombSites.has(value.site))) &&
    (value.position === undefined || value.position === null || isVector3(value.position)) &&
    (value.carrierPlayerId === undefined ||
      value.carrierPlayerId === null ||
      typeof value.carrierPlayerId === 'string') &&
    (value.carrierId === undefined || value.carrierId === null || typeof value.carrierId === 'string') &&
    (value.explodeInSeconds === undefined ||
      value.explodeInSeconds === null ||
      isFiniteNumber(value.explodeInSeconds)) &&
    (value.defuseInSeconds === undefined ||
      value.defuseInSeconds === null ||
      isFiniteNumber(value.defuseInSeconds)) &&
    (value.explodeAtMs === undefined || value.explodeAtMs === null || isFiniteNumber(value.explodeAtMs)) &&
    (value.defuseEndAtMs === undefined || value.defuseEndAtMs === null || isFiniteNumber(value.defuseEndAtMs))
  );
}

export function parseServerMessage(raw: string): ServerMessage | null {
  let value: unknown;
  try {
    value = JSON.parse(raw) as unknown;
  } catch {
    return null;
  }

  if (!isRecord(value) || value.v !== PROTOCOL_VERSION || typeof value.type !== 'string') {
    return null;
  }

  if (value.type === 'hello') {
    return isFiniteNumber(value.serverTimeMs) ? (value as unknown as HelloMessage) : null;
  }

  if (value.type === 'error') {
    return typeof value.code === 'string' && typeof value.message === 'string'
      ? (value as unknown as ErrorMessage)
      : null;
  }

  if (value.type !== 'snapshot') return null;
  if (!isRecord(value.map) || typeof value.map.id !== 'string') return null;
  if (!Array.isArray(value.players) || !value.players.every(isPlayer)) return null;
  if (!isBomb(value.bomb)) return null;
  if (!isFiniteNumber(value.seq) || !isFiniteNumber(value.capturedAtMs)) return null;
  if (
    value.localPlayerId !== undefined &&
    value.localPlayerId !== null &&
    typeof value.localPlayerId !== 'string'
  ) {
    return null;
  }
  if (
    value.observedPlayerId !== undefined &&
    value.observedPlayerId !== null &&
    typeof value.observedPlayerId !== 'string'
  ) {
    return null;
  }
  if (
    value.localTeam !== undefined &&
    (typeof value.localTeam !== 'string' ||
      !teams.has(value.localTeam as Team))
  ) {
    return null;
  }

  return value as unknown as SnapshotMessage;
}

export function isMapManifest(value: unknown): value is MapManifest {
  if (!isRecord(value) || value.version !== 1 || !Array.isArray(value.maps)) return false;
  if (
    value.source !== undefined &&
    (!isRecord(value.source) ||
      typeof value.source.project !== 'string' ||
      typeof value.source.release !== 'string')
  ) {
    return false;
  }
  return value.maps.every((map) => {
    if (!isRecord(map) || !isRecord(map.origin)) return false;
    const coreValid =
      typeof map.id === 'string' &&
      typeof map.name === 'string' &&
      isFiniteNumber(map.origin.x) &&
      isFiniteNumber(map.origin.y) &&
      isFiniteNumber(map.scale) &&
      map.scale > 0 &&
      (map.image === undefined || typeof map.image === 'string');
    if (!coreValid || map.levels === undefined) return coreValid;
    return (
      Array.isArray(map.levels) &&
      map.levels.every(
        (level) =>
          isRecord(level) &&
          typeof level.id === 'string' &&
          typeof level.image === 'string' &&
          isFiniteNumber(level.minZ) &&
          isFiniteNumber(level.maxZ) &&
          level.minZ <= level.maxZ,
      )
    );
  });
}
