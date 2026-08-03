import { useId, useState, type FormEvent, type ReactNode } from 'react';
import type { RelayAccessState } from '../hooks/useRelaySession';
import type { RelayLogin } from '../lib/session';

interface RelayAccessGateProps {
  access: RelayAccessState;
  submitting: boolean;
  actionError: string | null;
  onLogin: (login: RelayLogin) => Promise<boolean>;
  onRetry: () => void;
}

export function RelayAccessGate({
  access,
  submitting,
  actionError,
  onLogin,
  onRetry,
}: RelayAccessGateProps) {
  const [room, setRoom] = useState('');
  const [inviteToken, setInviteToken] = useState('');
  const titleId = useId();
  const loginDescriptionId = useId();
  const loginErrorId = useId();

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const normalizedRoom = room.trim();
    if (!normalizedRoom || !inviteToken) return;

    const pendingInvite = inviteToken;
    setInviteToken('');
    await onLogin({ room: normalizedRoom, inviteToken: pendingInvite });
  };

  let content: ReactNode;
  if (access.status === 'loading') {
    content = (
      <div className="access-message" role="status">
        <span className="access-spinner" aria-hidden="true" />
        <strong>正在检查安全会话</strong>
        <p>正在连接公网 Radar Relay…</p>
      </div>
    );
  } else if (access.status === 'unavailable') {
    content = (
      <div className="access-message" role="alert">
        <span className="access-symbol" aria-hidden="true">!</span>
        <strong>访问入口不可用</strong>
        <p>当前站点没有提供 Relay 会话接口。若这是本地内嵌 Radar，请从程序菜单重新复制带 token 的完整链接。</p>
      </div>
    );
  } else if (access.status === 'error') {
    content = (
      <div className="access-message" role="alert">
        <span className="access-symbol" aria-hidden="true">×</span>
        <strong>暂时无法连接 Relay</strong>
        <p>{access.message}</p>
        <button type="button" className="primary-button" onClick={onRetry}>重新检查</button>
      </div>
    );
  } else if (access.status === 'anonymous') {
    content = (
      <form className="relay-login" onSubmit={submit} autoComplete="off" aria-busy={submitting}>
        <div className="login-heading">
          <span className="access-symbol is-secure" aria-hidden="true">◆</span>
          <div>
            <strong>加入共享 Radar</strong>
            <p id={loginDescriptionId}>凭据仅用于换取当前站点的安全会话，页面不会将其写入 URL 或 Web Storage。</p>
          </div>
        </div>

        <label>
          <span>房间</span>
          <input
            name="room"
            value={room}
            onChange={(event) => setRoom(event.target.value)}
            autoComplete="username"
            autoCapitalize="none"
            spellCheck={false}
            maxLength={64}
            required
            disabled={submitting}
            placeholder="例如 match-alpha"
          />
        </label>

        <label>
          <span>邀请凭据</span>
          <input
            name="inviteToken"
            type="password"
            value={inviteToken}
            onChange={(event) => setInviteToken(event.target.value)}
            autoComplete="one-time-code"
            maxLength={512}
            required
            disabled={submitting}
            placeholder="输入该房间的 Viewer 邀请凭据"
            aria-invalid={actionError !== null}
            aria-describedby={`${loginDescriptionId}${actionError ? ` ${loginErrorId}` : ''}`}
          />
        </label>

        {actionError && <p id={loginErrorId} className="login-error" role="alert">{actionError}</p>}

        <button
          type="submit"
          className="primary-button"
          disabled={submitting || room.trim().length === 0 || inviteToken.length === 0}
        >
          {submitting ? '正在验证…' : '安全进入'}
        </button>
      </form>
    );
  } else {
    content = null;
  }

  return (
    <main className="access-shell">
      <section className="access-card" aria-labelledby={titleId}>
        <div className="access-brand">
          <span className="brand-mark" aria-hidden="true"><i /><i /><i /></span>
          <div><h1 id={titleId}>TACTICAL MAP</h1><span>SECURE RADAR RELAY</span></div>
        </div>
        {content}
        <footer>固定北向地图 · 只读实时数据 · HTTPS/WSS</footer>
      </section>
    </main>
  );
}
