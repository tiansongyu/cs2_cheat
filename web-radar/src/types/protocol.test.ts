import { describe, expect, it } from 'vitest';
import { parseServerMessage } from './protocol';

const validSnapshot = {
  v: 1,
  type: 'snapshot',
  seq: 42,
  capturedAtMs: 100_500,
  map: { id: 'de_example' },
  localPlayerId: 'p1',
  observedPlayerId: 'p1',
  players: [
    {
      id: 'p1',
      name: 'Player',
      team: 'CT',
      alive: true,
      position: { x: 1, y: 2, z: 3 },
      yaw: 90,
      health: 100,
      armor: 75,
      money: 3_250,
      weapons: [{ name: 'weapon_m4a1', active: true }],
    },
  ],
  bomb: { state: 'carried', carrierId: 'p1' },
};

describe('parseServerMessage', () => {
  it('accepts a structurally valid v1 snapshot', () => {
    const parsed = parseServerMessage(JSON.stringify(validSnapshot));
    expect(parsed?.type).toBe('snapshot');
    if (parsed?.type === 'snapshot') expect(parsed.players[0].name).toBe('Player');
  });

  it('accepts an older v1 snapshot without observedPlayerId', () => {
    const legacySnapshot = structuredClone(validSnapshot);
    Reflect.deleteProperty(legacySnapshot, 'observedPlayerId');

    const parsed = parseServerMessage(JSON.stringify(legacySnapshot));
    expect(parsed?.type).toBe('snapshot');
    if (parsed?.type === 'snapshot') {
      expect(parsed.observedPlayerId).toBeUndefined();
    }
  });

  it('rejects other versions, malformed teams and invalid JSON', () => {
    expect(parseServerMessage(JSON.stringify({ ...validSnapshot, v: 2 }))).toBeNull();
    const invalidTeam = structuredClone(validSnapshot);
    invalidTeam.players[0].team = 'BLUE';
    expect(parseServerMessage(JSON.stringify(invalidTeam))).toBeNull();
    expect(parseServerMessage('{nope')).toBeNull();
  });

  it('accepts hello and error control frames', () => {
    expect(parseServerMessage('{"v":1,"type":"hello","serverTimeMs":12}')?.type).toBe('hello');
    expect(
      parseServerMessage('{"v":1,"type":"error","code":"auth","message":"denied"}')?.type,
    ).toBe('error');
  });

  it('accepts nullable tracking fields and the rich C++ serializer shape', () => {
    const message = {
      ...validSnapshot,
      protocolVersion: 1,
      localPlayerId: null,
      observedPlayerId: null,
      localTeam: 'NONE',
      map: {
        id: 'de_example',
        displayName: 'Example',
        phase: 'loading',
        roundNumber: 0,
        connected: false,
      },
      players: [
        {
          id: '76561190000000000',
          steamId: null,
          name: null,
          team: 'T',
          competitiveColor: -1,
          alive: false,
          dormant: true,
          position: null,
          yaw: null,
          health: 0,
          armor: 0,
          money: 0,
          hasHelmet: false,
          hasDefuser: false,
          hasBomb: false,
          activeWeapon: {
            definitionIndex: 7,
            name: 'weapon_ak47',
            displayName: 'AK-47',
            category: 'rifle',
            clipAmmo: 0,
            reserveAmmo: 0,
          },
          inventory: [],
        },
      ],
      bomb: {
        state: 'unknown',
        site: 'unknown',
        position: null,
        carrierPlayerId: null,
        explodeInSeconds: null,
        beingDefused: false,
        defuseInSeconds: null,
        defuseWillSucceed: false,
      },
    };
    expect(parseServerMessage(JSON.stringify(message))?.type).toBe('snapshot');
  });

  it('rejects a non-string observed player identifier', () => {
    expect(parseServerMessage(JSON.stringify({
      ...validSnapshot,
      observedPlayerId: 123,
    }))).toBeNull();
  });

  it('rejects a local team outside the v1 team enum', () => {
    expect(parseServerMessage(JSON.stringify({
      ...validSnapshot,
      localTeam: 'BLUE',
    }))).toBeNull();
  });
});
