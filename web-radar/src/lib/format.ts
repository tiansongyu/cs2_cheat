import type { BombState, Team, WeaponSnapshot } from '../types/protocol';

export function teamLabel(team: Team): string {
  if (team === 'CT') return '反恐精英';
  if (team === 'T') return '恐怖分子';
  if (team === 'SPEC') return '观战';
  return '未分队';
}

export function bombStateLabel(state: BombState): string {
  const labels: Record<BombState, string> = {
    unknown: '状态未知',
    carried: '携带中',
    dropped: '已掉落',
    planted: '已安放',
    defused: '已拆除',
    exploded: '已爆炸',
  };
  return labels[state];
}

export function compactWeaponName(weapon: WeaponSnapshot): string {
  return (weapon.displayName || weapon.name)
    .replace(/^weapon_/i, '')
    .replace(/_/g, ' ')
    .toUpperCase();
}
