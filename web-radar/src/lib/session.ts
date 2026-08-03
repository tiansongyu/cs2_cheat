const SESSION_ENDPOINT = '/api/v1/session';

export interface RelaySession {
  room: string;
  expiresAtMs: number;
}

export interface RelayLogin {
  room: string;
  inviteToken: string;
}

export type RelaySessionProbe =
  | { kind: 'authenticated'; session: RelaySession }
  | { kind: 'unauthenticated' }
  | { kind: 'unavailable' };

export type RelaySessionErrorKind =
  | 'invalid-credentials'
  | 'unavailable'
  | 'protocol'
  | 'request';

export class RelaySessionError extends Error {
  readonly kind: RelaySessionErrorKind;
  readonly status: number | null;

  constructor(kind: RelaySessionErrorKind, message: string, status: number | null = null) {
    super(message);
    this.name = 'RelaySessionError';
    this.kind = kind;
    this.status = status;
  }
}

type FetchLike = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

function parseSession(value: unknown): RelaySession {
  if (!value || typeof value !== 'object') {
    throw new RelaySessionError('protocol', 'Relay 返回了无效的会话信息');
  }

  const candidate = value as Record<string, unknown>;
  if (
    candidate.authenticated !== true
    || typeof candidate.room !== 'string'
    || candidate.room.length === 0
    || candidate.room.length > 64
    || typeof candidate.expiresAtMs !== 'number'
    || !Number.isSafeInteger(candidate.expiresAtMs)
    || candidate.expiresAtMs < 0
  ) {
    throw new RelaySessionError('protocol', 'Relay 返回了无效的会话信息');
  }

  return { room: candidate.room, expiresAtMs: candidate.expiresAtMs };
}

async function readSession(response: Response): Promise<RelaySession> {
  try {
    return parseSession(await response.json());
  } catch (error) {
    if (error instanceof RelaySessionError) throw error;
    throw new RelaySessionError('protocol', 'Relay 返回了无法解析的会话信息');
  }
}

export async function probeRelaySession(
  fetcher: FetchLike = fetch,
  signal?: AbortSignal,
): Promise<RelaySessionProbe> {
  let response: Response;
  try {
    response = await fetcher(SESSION_ENDPOINT, {
      method: 'GET',
      credentials: 'same-origin',
      cache: 'no-store',
      redirect: 'error',
      headers: { Accept: 'application/json' },
      signal,
    });
  } catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw error;
    throw new RelaySessionError('request', '无法连接公网 Radar Relay');
  }

  if (response.status === 401) return { kind: 'unauthenticated' };
  if (response.status === 403) {
    throw new RelaySessionError('request', 'Radar Relay 的站点来源配置不匹配', response.status);
  }
  if (response.status === 404) return { kind: 'unavailable' };
  if (!response.ok) {
    throw new RelaySessionError('request', 'Radar Relay 暂时不可用', response.status);
  }

  return { kind: 'authenticated', session: await readSession(response) };
}

export async function loginRelaySession(
  login: RelayLogin,
  fetcher: FetchLike = fetch,
  signal?: AbortSignal,
): Promise<RelaySession> {
  let response: Response;
  try {
    response = await fetcher(SESSION_ENDPOINT, {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      redirect: 'error',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(login),
      signal,
    });
  } catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw error;
    throw new RelaySessionError('request', '无法连接公网 Radar Relay');
  }

  if (response.status === 401) {
    throw new RelaySessionError('invalid-credentials', '房间或邀请凭据无效', response.status);
  }
  if (response.status === 403) {
    throw new RelaySessionError('request', 'Radar Relay 的站点来源配置不匹配', response.status);
  }
  if (response.status === 404) {
    throw new RelaySessionError('unavailable', '当前站点未启用 Radar Relay', response.status);
  }
  if (!response.ok) {
    throw new RelaySessionError('request', '登录暂时失败，请稍后重试', response.status);
  }

  return readSession(response);
}

export async function logoutRelaySession(fetcher: FetchLike = fetch): Promise<void> {
  let response: Response;
  try {
    response = await fetcher(SESSION_ENDPOINT, {
      method: 'DELETE',
      credentials: 'same-origin',
      cache: 'no-store',
      redirect: 'error',
      headers: { Accept: 'application/json' },
    });
  } catch {
    throw new RelaySessionError('request', '无法连接公网 Radar Relay');
  }

  // An expired or already-revoked cookie has the same local result as logout.
  if (response.ok || response.status === 401) return;
  throw new RelaySessionError('request', '退出登录失败，请稍后重试', response.status);
}
