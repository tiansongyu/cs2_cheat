#!/usr/bin/env node

import { lstat, readFile, realpath } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const pngSignature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
const mapIdPattern = /^[A-Za-z0-9_-]{1,64}$/;
const sha256Pattern = /^[0-9a-f]{64}$/;

function invalid(message) {
  throw new Error(message);
}

function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function isInside(root, candidate) {
  const relative = path.relative(root, candidate);
  return relative !== ''
    && relative !== '..'
    && !relative.startsWith(`..${path.sep}`)
    && !path.isAbsolute(relative);
}

async function readRegularFile(bundleRoot, relativePath, label) {
  const segments = relativePath.split('/');
  const candidate = path.resolve(bundleRoot, ...segments);
  if (!isInside(bundleRoot, candidate)) {
    invalid(`${label} escapes the bundle root: ${relativePath}`);
  }

  let information;
  try {
    information = await lstat(candidate);
  } catch {
    invalid(`${label} is missing: ${relativePath}`);
  }
  if (!information.isFile() || information.isSymbolicLink()) {
    invalid(`${label} must be a regular file, not a symlink: ${relativePath}`);
  }

  const resolved = await realpath(candidate);
  if (!isInside(bundleRoot, resolved)) {
    invalid(`${label} resolves outside the bundle root: ${relativePath}`);
  }
  return readFile(resolved);
}

function imagePathForBundle(image, mapId, label) {
  if (typeof image !== 'string' || image.length === 0) {
    invalid(`${label} must be a non-empty string`);
  }
  if (
    !image.startsWith('/maps/')
    || image.startsWith('//')
    || image.includes('\\')
    || image.includes('?')
    || image.includes('#')
    || image.includes('%')
    || /[\u0000-\u001f\u007f]/u.test(image)
  ) {
    invalid(`${label} must be a canonical local /maps/ PNG path: ${image}`);
  }

  const relativePath = image.slice(1);
  const segments = relativePath.split('/');
  if (
    segments.some((segment) => segment.length === 0 || segment === '.' || segment === '..')
    || path.posix.normalize(relativePath) !== relativePath
    || !relativePath.endsWith('.png')
  ) {
    invalid(`${label} contains an unsafe or non-PNG path: ${image}`);
  }
  if (segments.length < 3 || segments[0] !== 'maps' || segments[1] !== mapId) {
    invalid(`${label} must remain inside /maps/${mapId}/: ${image}`);
  }
  return relativePath;
}

async function validatePng(bundleRoot, relativePath, label) {
  const payload = await readRegularFile(bundleRoot, relativePath, label);
  if (payload.length === 0) {
    invalid(`${label} is empty: ${relativePath}`);
  }
  if (payload.length < pngSignature.length || !payload.subarray(0, 8).equals(pngSignature)) {
    invalid(`${label} does not have a valid PNG signature: ${relativePath}`);
  }
  if (
    payload.length < 24
    || payload.subarray(12, 16).toString('ascii') !== 'IHDR'
    || payload.readUInt32BE(16) !== 1024
    || payload.readUInt32BE(20) !== 1024
  ) {
    invalid(`${label} must be a 1024 x 1024 PNG: ${relativePath}`);
  }
}

async function validateBundle(bundleDirectory) {
  const requestedRoot = path.resolve(bundleDirectory);
  let rootInformation;
  try {
    rootInformation = await lstat(requestedRoot);
  } catch {
    invalid(`bundle root does not exist: ${requestedRoot}`);
  }
  if (!rootInformation.isDirectory() || rootInformation.isSymbolicLink()) {
    invalid(`bundle root must be a directory, not a symlink: ${requestedRoot}`);
  }
  const bundleRoot = await realpath(requestedRoot);

  const manifestPayload = await readRegularFile(
    bundleRoot,
    'maps/manifest.json',
    'map manifest',
  );
  let manifest;
  try {
    manifest = JSON.parse(manifestPayload.toString('utf8'));
  } catch {
    invalid('maps/manifest.json is not valid UTF-8 JSON');
  }
  if (!isRecord(manifest) || manifest.version !== 1 || !Array.isArray(manifest.maps)) {
    invalid('maps/manifest.json must contain a v1 maps array');
  }
  if (manifest.maps.length === 0) {
    invalid('maps/manifest.json must contain at least one map');
  }

  const mapIds = new Set();
  const imagePaths = new Set();
  let dust2MainImage = null;
  for (const [mapIndex, mapEntry] of manifest.maps.entries()) {
    const mapLabel = `maps[${mapIndex}]`;
    if (!isRecord(mapEntry) || typeof mapEntry.id !== 'string') {
      invalid(`${mapLabel} must be an object with a string id`);
    }
    if (!mapIdPattern.test(mapEntry.id)) {
      invalid(`${mapLabel}.id contains unsupported characters: ${mapEntry.id}`);
    }
    if (mapIds.has(mapEntry.id)) {
      invalid(`maps/manifest.json contains duplicate map id: ${mapEntry.id}`);
    }
    mapIds.add(mapEntry.id);

    const mainImage = mapEntry.image === undefined
      ? `/maps/${mapEntry.id}/radar.png`
      : mapEntry.image;
    const mainPath = imagePathForBundle(mainImage, mapEntry.id, `${mapLabel}.image`);
    imagePaths.add(mainPath);
    if (mapEntry.id === 'de_dust2') {
      dust2MainImage = mainPath;
    }

    if (mapEntry.levels !== undefined) {
      if (!Array.isArray(mapEntry.levels)) {
        invalid(`${mapLabel}.levels must be an array when present`);
      }
      for (const [levelIndex, level] of mapEntry.levels.entries()) {
        const levelLabel = `${mapLabel}.levels[${levelIndex}]`;
        if (!isRecord(level)) {
          invalid(`${levelLabel} must be an object`);
        }
        const levelPath = imagePathForBundle(
          level.image,
          mapEntry.id,
          `${levelLabel}.image`,
        );
        imagePaths.add(levelPath);
      }
    }
  }

  if (!mapIds.has('de_dust2')) {
    invalid('maps/manifest.json must contain de_dust2');
  }
  if (dust2MainImage !== 'maps/de_dust2/radar.png') {
    invalid('de_dust2 must resolve to /maps/de_dust2/radar.png');
  }

  for (const imagePath of [...imagePaths].sort()) {
    await validatePng(bundleRoot, imagePath, `map image ${imagePath}`);
  }

  const notice = await readRegularFile(bundleRoot, 'maps/NOTICE.txt', 'map NOTICE');
  if (notice.toString('utf8').trim().length === 0) {
    invalid('maps/NOTICE.txt must not be empty');
  }

  const sourcePayload = await readRegularFile(bundleRoot, 'maps/SOURCE.json', 'map SOURCE');
  let source;
  try {
    source = JSON.parse(sourcePayload.toString('utf8'));
  } catch {
    invalid('maps/SOURCE.json is not valid UTF-8 JSON');
  }
  if (
    !isRecord(source)
    || typeof source.release !== 'string'
    || source.release.length === 0
    || typeof source.images_sha256 !== 'string'
    || !sha256Pattern.test(source.images_sha256)
    || typeof source.map_data_sha256 !== 'string'
    || !sha256Pattern.test(source.map_data_sha256)
  ) {
    invalid('maps/SOURCE.json must contain a release and two lowercase SHA-256 digests');
  }

  return { maps: mapIds.size, images: imagePaths.size };
}

async function main() {
  if (process.argv.length !== 3 || process.argv[2] === '--help') {
    const stream = process.argv[2] === '--help' ? process.stdout : process.stderr;
    stream.write(`Usage: node ${path.basename(process.argv[1])} <web-radar-bundle-root>\n`);
    process.exitCode = process.argv[2] === '--help' ? 0 : 2;
    return;
  }

  try {
    const result = await validateBundle(process.argv[2]);
    process.stdout.write(
      `Web Radar bundle valid: ${result.maps} maps, ${result.images} unique PNG files\n`,
    );
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    process.stderr.write(`Web Radar bundle validation failed: ${message}\n`);
    process.exitCode = 1;
  }
}

await main();
