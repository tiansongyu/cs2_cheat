import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it, vi } from 'vitest';
import { ConnectionNotice } from './ConnectionNotice';

describe('ConnectionNotice', () => {
  it('stays out of the way while a healthy stream is live', () => {
    const html = renderToStaticMarkup(createElement(ConnectionNotice, {
      status: 'connected',
      stale: false,
      error: null,
      retryInMs: null,
      lastReceivedAtMs: null,
      hasFrame: true,
      onRetry: vi.fn(),
    }));
    expect(html).toBe('');
  });

  it('explains reconnect timing and exposes a manual retry action', () => {
    const html = renderToStaticMarkup(createElement(ConnectionNotice, {
      status: 'reconnecting',
      stale: true,
      error: '公网 Radar 连接已中断',
      retryInMs: 1_500,
      lastReceivedAtMs: Date.UTC(2026, 7, 3, 8, 9, 10),
      hasFrame: true,
      onRetry: vi.fn(),
    }));
    expect(html).toContain('实时连接已中断');
    expect(html).toContain('约 2 秒后自动重试');
    expect(html).toContain('立即重试');
    expect(html).toContain('role="status"');
  });

  it('distinguishes an offline browser from an unavailable producer', () => {
    const html = renderToStaticMarkup(createElement(ConnectionNotice, {
      status: 'offline',
      stale: true,
      error: null,
      retryInMs: null,
      lastReceivedAtMs: null,
      hasFrame: false,
      onRetry: vi.fn(),
    }));
    expect(html).toContain('设备当前离线');
    expect(html).toContain('网络恢复后会自动重连');
  });
});
