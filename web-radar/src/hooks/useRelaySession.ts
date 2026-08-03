import { useCallback, useEffect, useRef, useState } from 'react';
import type { RadarDeploymentMode } from '../lib/deployment';
import {
  loginRelaySession,
  logoutRelaySession,
  probeRelaySession,
  RelaySessionError,
  type RelayLogin,
  type RelaySession,
} from '../lib/session';

export type RelayAccessState =
  | { status: 'bypassed' }
  | { status: 'loading' }
  | { status: 'anonymous' }
  | { status: 'unavailable' }
  | { status: 'error'; message: string }
  | { status: 'authenticated'; session: RelaySession };

export interface RelaySessionController {
  access: RelayAccessState;
  submitting: boolean;
  actionError: string | null;
  login: (login: RelayLogin) => Promise<boolean>;
  logout: () => Promise<void>;
  retry: () => void;
  invalidate: () => void;
}

function messageForError(error: unknown): string {
  return error instanceof RelaySessionError ? error.message : 'Radar Relay 请求失败';
}

export function useRelaySession(mode: RadarDeploymentMode): RelaySessionController {
  const [access, setAccess] = useState<RelayAccessState>(
    mode === 'embedded' ? { status: 'bypassed' } : { status: 'loading' },
  );
  const [submitting, setSubmitting] = useState(false);
  const [actionError, setActionError] = useState<string | null>(null);
  const [probeVersion, setProbeVersion] = useState(0);
  const operationController = useRef<AbortController | null>(null);

  useEffect(() => () => {
    const controller = operationController.current;
    operationController.current = null;
    controller?.abort();
  }, []);

  useEffect(() => {
    if (mode === 'embedded') {
      setAccess({ status: 'bypassed' });
      return undefined;
    }

    const controller = new AbortController();
    setAccess({ status: 'loading' });
    setActionError(null);

    void probeRelaySession(fetch, controller.signal)
      .then((probe) => {
        if (controller.signal.aborted) return;
        if (probe.kind === 'authenticated') {
          setAccess({ status: 'authenticated', session: probe.session });
        } else {
          setAccess({ status: probe.kind === 'unavailable' ? 'unavailable' : 'anonymous' });
        }
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;
        setAccess({ status: 'error', message: messageForError(error) });
      });

    return () => controller.abort();
  }, [mode, probeVersion]);

  useEffect(() => {
    if (access.status !== 'authenticated') return undefined;
    const remaining = access.session.expiresAtMs - Date.now();
    if (remaining <= 0) {
      setAccess({ status: 'anonymous' });
      return undefined;
    }

    const timer = window.setTimeout(() => {
      if (access.session.expiresAtMs <= Date.now()) {
        setActionError('会话已过期，请重新登录');
        setAccess({ status: 'anonymous' });
        return;
      }
      // Browser timers are limited to a signed 32-bit delay. Re-arm the
      // effect for sessions whose expiry is further in the future.
      setAccess((current) => current.status === 'authenticated' ? { ...current } : current);
    }, Math.min(remaining, 2_147_483_647));
    return () => window.clearTimeout(timer);
  }, [access]);

  useEffect(() => {
    if (mode === 'embedded') return undefined;
    const resume = () => {
      if (document.visibilityState === 'hidden') return;
      if (access.status === 'authenticated' && access.session.expiresAtMs <= Date.now()) {
        setActionError('会话已过期，请重新登录');
        setAccess({ status: 'anonymous' });
      } else if (access.status === 'error') {
        setProbeVersion((version) => version + 1);
      }
    };
    window.addEventListener('online', resume);
    window.addEventListener('pageshow', resume);
    document.addEventListener('visibilitychange', resume);
    return () => {
      window.removeEventListener('online', resume);
      window.removeEventListener('pageshow', resume);
      document.removeEventListener('visibilitychange', resume);
    };
  }, [access, mode]);

  const login = useCallback(async (credentials: RelayLogin): Promise<boolean> => {
    operationController.current?.abort();
    const controller = new AbortController();
    operationController.current = controller;
    setSubmitting(true);
    setActionError(null);
    try {
      const session = await loginRelaySession(credentials, fetch, controller.signal);
      if (controller.signal.aborted) return false;
      setAccess({ status: 'authenticated', session });
      return true;
    } catch (error) {
      if (controller.signal.aborted) return false;
      setActionError(messageForError(error));
      if (error instanceof RelaySessionError && error.kind === 'unavailable') {
        setAccess({ status: 'unavailable' });
      } else {
        setAccess({ status: 'anonymous' });
      }
      return false;
    } finally {
      if (operationController.current === controller) {
        operationController.current = null;
        setSubmitting(false);
      }
    }
  }, []);

  const logout = useCallback(async () => {
    operationController.current?.abort();
    const controller = new AbortController();
    operationController.current = controller;
    // Stop rendering the protected stream before the network request finishes.
    setAccess({ status: 'anonymous' });
    setSubmitting(true);
    setActionError(null);
    try {
      await logoutRelaySession(fetch, controller.signal);
    } catch (error) {
      if (!controller.signal.aborted) setActionError(messageForError(error));
    } finally {
      if (operationController.current === controller) {
        operationController.current = null;
        setSubmitting(false);
      }
    }
  }, []);

  const retry = useCallback(() => setProbeVersion((version) => version + 1), []);
  const invalidate = useCallback(() => {
    setActionError('会话已失效，请重新登录');
    setAccess({ status: 'anonymous' });
  }, []);

  return { access, submitting, actionError, login, logout, retry, invalidate };
}
