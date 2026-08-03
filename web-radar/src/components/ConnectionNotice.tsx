import type { StreamStatus } from '../lib/stream';

interface ConnectionNoticeProps {
  status: StreamStatus;
  stale: boolean;
  error: string | null;
  retryInMs: number | null;
  lastReceivedAtMs: number | null;
  hasFrame: boolean;
  onRetry: () => void;
}

function formatLastSnapshot(timestamp: number | null): string | null {
  if (timestamp === null) return null;
  return new Intl.DateTimeFormat(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  }).format(new Date(timestamp));
}

export function ConnectionNotice({
  status,
  stale,
  error,
  retryInMs,
  lastReceivedAtMs,
  hasFrame,
  onRetry,
}: ConnectionNoticeProps) {
  if (status === 'connected' && !stale && !error) return null;

  let heading = '连接状态异常';
  let detail = error ?? '正在恢复 Radar 数据流';
  if (status === 'offline') {
    heading = '设备当前离线';
    detail = '网络恢复后会自动重连，也可以手动重试。';
  } else if (status === 'connecting') {
    heading = '正在连接 Radar';
    detail = '正在建立安全的实时数据连接…';
  } else if (status === 'reconnecting') {
    heading = '实时连接已中断';
    detail = retryInMs === null
      ? (error ?? '正在重新连接…')
      : `${error ?? '连接暂时不可用'}；约 ${Math.max(1, Math.ceil(retryInMs / 1_000))} 秒后自动重试。`;
  } else if (status === 'connected' && stale) {
    heading = hasFrame ? '游戏数据已暂停' : '已连接，正在等待首个快照';
    detail = hasFrame
      ? '超过 3 秒未收到新快照；可能是 Producer 断线、游戏暂停或页面刚从后台恢复。'
      : 'Relay 已连接，但 Producer 尚未发布游戏数据。';
  } else if (status === 'disconnected') {
    heading = 'Radar 会话已断开';
  }

  const lastSnapshot = formatLastSnapshot(lastReceivedAtMs);
  return (
    <aside className="connection-notice" role="status" aria-live="polite" aria-atomic="true">
      <span className="connection-notice-mark" aria-hidden="true">!</span>
      <div>
        <strong>{heading}</strong>
        <span>{detail}</span>
        {lastSnapshot && <small>最近快照：{lastSnapshot}</small>}
      </div>
      <button type="button" onClick={onRetry}>立即重试</button>
    </aside>
  );
}
