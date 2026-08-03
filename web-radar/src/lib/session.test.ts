import { describe, expect, it, vi } from 'vitest';
import {
  loginRelaySession,
  logoutRelaySession,
  probeRelaySession,
} from './session';

const sessionBody = {
  authenticated: true,
  room: 'match-alpha',
  expiresAtMs: 1_900_000_000_000,
};

function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json' },
  });
}

describe('probeRelaySession', () => {
  it('loads an authenticated HttpOnly-cookie session without URL credentials', async () => {
    const fetcher = vi.fn(async (_input: RequestInfo | URL, _init?: RequestInit) => (
      jsonResponse(sessionBody)
    ));
    await expect(probeRelaySession(fetcher)).resolves.toEqual({
      kind: 'authenticated',
      session: { room: 'match-alpha', expiresAtMs: 1_900_000_000_000 },
    });

    expect(fetcher).toHaveBeenCalledWith('/api/v1/session', expect.objectContaining({
      method: 'GET',
      credentials: 'same-origin',
      cache: 'no-store',
    }));
  });

  it('distinguishes an unauthenticated relay from an embedded page missing token', async () => {
    const unauthenticated = vi.fn(async () => new Response(null, { status: 401 }));
    const unavailable = vi.fn(async () => new Response(null, { status: 404 }));

    await expect(probeRelaySession(unauthenticated)).resolves.toEqual({ kind: 'unauthenticated' });
    await expect(probeRelaySession(unavailable)).resolves.toEqual({ kind: 'unavailable' });
  });
});

describe('relay login and logout', () => {
  it('sends the invite only in a same-origin JSON POST and returns the cookie session', async () => {
    const fetcher = vi.fn(async (_input: RequestInfo | URL, _init?: RequestInit) => (
      jsonResponse(sessionBody)
    ));
    await expect(loginRelaySession({ room: 'match-alpha', inviteToken: 'secret' }, fetcher))
      .resolves.toEqual({ room: 'match-alpha', expiresAtMs: 1_900_000_000_000 });

    const [url, init] = fetcher.mock.calls[0];
    expect(url).toBe('/api/v1/session');
    expect(init).toEqual(expect.objectContaining({
      method: 'POST',
      credentials: 'same-origin',
      redirect: 'error',
      body: JSON.stringify({ room: 'match-alpha', inviteToken: 'secret' }),
    }));
    expect(String(url)).not.toContain('secret');
  });

  it('maps 401 to invalid credentials', async () => {
    const fetcher = vi.fn(async () => new Response(null, { status: 401 }));
    const failure = loginRelaySession({ room: 'room', inviteToken: 'bad' }, fetcher);
    await expect(failure).rejects.toMatchObject({
      kind: 'invalid-credentials',
      status: 401,
    });
  });

  it('reports an exact-Origin deployment mismatch separately', async () => {
    const fetcher = vi.fn(async () => new Response(null, { status: 403 }));
    const failure = loginRelaySession({ room: 'room', inviteToken: 'secret' }, fetcher);
    await expect(failure).rejects.toMatchObject({
      kind: 'request',
      status: 403,
    });
  });

  it('logs out with DELETE and accepts an already-expired session', async () => {
    const fetcher = vi.fn(async () => new Response(null, { status: 401 }));
    await expect(logoutRelaySession(fetcher)).resolves.toBeUndefined();
    expect(fetcher).toHaveBeenCalledWith('/api/v1/session', expect.objectContaining({
      method: 'DELETE',
      credentials: 'same-origin',
    }));
  });

  it('does not claim logout succeeded when Origin validation fails', async () => {
    const fetcher = vi.fn(async () => new Response(null, { status: 403 }));
    await expect(logoutRelaySession(fetcher)).rejects.toMatchObject({
      kind: 'request',
      status: 403,
    });
  });
});
