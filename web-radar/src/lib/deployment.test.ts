import { describe, expect, it } from 'vitest';
import { resolveRadarDeployment, sanitizeRelayUrl } from './deployment';

describe('resolveRadarDeployment', () => {
  it('selects embedded mode only for a non-empty CivetWeb token', () => {
    expect(resolveRadarDeployment({ search: '?token=abc' })).toBe('embedded');
    expect(resolveRadarDeployment({ search: '?token=' })).toBe('relay');
  });

  it('selects relay mode without putting relay credentials in the URL', () => {
    expect(resolveRadarDeployment({ search: '' })).toBe('relay');
    expect(resolveRadarDeployment({ search: '?theme=dark' })).toBe('relay');
  });

  it('removes credential-shaped Relay parameters while preserving harmless options', () => {
    expect(sanitizeRelayUrl({
      pathname: '/radar',
      search: '?inviteToken=secret&theme=dark&token=',
    })).toBe('/radar?theme=dark');
    expect(sanitizeRelayUrl({ pathname: '/', search: '?theme=dark' })).toBeNull();
  });
});
