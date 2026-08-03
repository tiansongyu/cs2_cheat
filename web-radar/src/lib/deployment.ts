export type RadarDeploymentMode = 'embedded' | 'relay';

interface LocationSearchLike {
  search: string;
}

/**
 * A token in the page URL is the explicit marker for the embedded CivetWeb
 * deployment. Relay pages intentionally keep all credentials out of the URL.
 */
export function resolveRadarDeployment(location: LocationSearchLike): RadarDeploymentMode {
  return new URLSearchParams(location.search).has('token') ? 'embedded' : 'relay';
}
