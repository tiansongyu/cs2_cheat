import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const generator = fileURLToPath(new URL('../scripts/generate_skin_build_info.mjs', import.meta.url));
test('diagnostic metadata follows actual input headers and rejects stale or invalid input', () => {
  const root = mkdtempSync(join(tmpdir(), 'cs2-skin-build-'));
  try {
    const headers = join(root, 'external-cheat-base/generated');
    const skins = join(root, 'external-cheat-base/src/features/skins');
    mkdirSync(headers, { recursive: true });
    mkdirSync(skins, { recursive: true });
    writeFileSync(join(root, 'last_commit.txt'), 'a'.repeat(40));
    writeFileSync(join(skins, 'SOURCE.json'), JSON.stringify({ revision: 'b'.repeat(40) }));
    const header = '// Generated using cs2-dumper\n// 2026-10-23 00:00:00 UTC\nnamespace cs2_dumper {}\n';
    for (const name of ['client_dll.hpp', 'offsets.hpp', 'buttons.hpp']) writeFileSync(join(headers, name), header);
    const run = (...args) => spawnSync(process.execPath, [generator, '--root', root, ...args], { encoding: 'utf8' });
    assert.equal(run().status, 0);
    assert.equal(run('--check').status, 0);
    const path = join(skins, 'skin_build_info.hpp');
    const original = readFileSync(path, 'utf8');
    assert.ok(original.includes(createHash('sha256').update(header).digest('hex')));
    assert.ok(original.includes('2026-10-23'));
    writeFileSync(join(headers, 'offsets.hpp'), header + '// changed\n');
    assert.notEqual(run('--check').status, 0);
    assert.equal(run().status, 0);
    assert.notEqual(readFileSync(path, 'utf8'), original);
    writeFileSync(join(root, 'last_commit.txt'), 'invalid revision');
    assert.notEqual(run().status, 0);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
