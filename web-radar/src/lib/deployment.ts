export type RadarDeploymentMode = 'embedded' | 'relay';

interface LocationSearchLike {
  search: string;
}

interface RelayLocationLike extends LocationSearchLike {
  pathname: string;
}

/**
 * A token in the page URL is the explicit marker for the embedded CivetWeb
 * deployment. Relay pages intentionally keep all credentials out of the URL.
 */
export function resolveRadarDeployment(location: LocationSearchLike): RadarDeploymentMode {
  const token = new URLSearchParams(location.search).get('token');
  return token ? 'embedded' : 'relay';
}

/**
 * Removes credential-shaped parameters from a Relay page without touching
 * harmless display/debug parameters. Embedded mode never calls this helper.
 */
export function sanitizeRelayUrl(location: RelayLocationLike): string | null {
  const params = new URLSearchParams(location.search);
  const sensitiveKeys = ['token', 'invite', 'inviteToken', 'access_token'];
  let changed = false;
  for (const key of sensitiveKeys) {
    if (params.has(key)) {
      params.delete(key);
      changed = true;
    }
  }
  if (!changed) return null;
  const query = params.toString();
  return `${location.pathname}${query ? `?${query}` : ''}`;
}
