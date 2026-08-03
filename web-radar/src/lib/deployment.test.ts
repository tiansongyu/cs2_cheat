import { describe, expect, it } from 'vitest';
import { resolveRadarDeployment } from './deployment';

describe('resolveRadarDeployment', () => {
  it('selects embedded mode whenever a token parameter is present', () => {
    expect(resolveRadarDeployment({ search: '?token=abc' })).toBe('embedded');
    expect(resolveRadarDeployment({ search: '?token=' })).toBe('embedded');
  });

  it('selects relay mode without putting relay credentials in the URL', () => {
    expect(resolveRadarDeployment({ search: '' })).toBe('relay');
    expect(resolveRadarDeployment({ search: '?theme=dark' })).toBe('relay');
  });
});
