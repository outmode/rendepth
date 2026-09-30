/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const script = await readFile(new URL('./extension/quality.js', import.meta.url), 'utf8');
function fixture({enabled = true, host = 'www.youtube.com', available = ['hd2160', 'hd1080', 'hd720'], playerMissing = false} = {}) {
  let now = 0;
  const calls = [], notices = [];
  const player = {getAvailableQualityLevels: () => available,
    setPlaybackQualityRange: (...args) => calls.push(args)};
  const source = {videoWidth: 3840, videoHeight: 2160, readyState: 4, isConnected: true,
    closest: () => playerMissing ? null : {wrappedJSObject: player}};
  const context = {window: {location: {hostname: host, href: `https://${host}/watch?v=test`}},
    performance: {now: () => now}};
  const completion = vm.runInNewContext(script, context);
  assert.equal(completion, undefined, 'Script injection must not return the helper function');
  assert.doesNotThrow(() => structuredClone(completion));
  const check = context.rendepthCreateQualityLimiter(enabled, text => notices.push(text));
  return {source, player, calls, notices, location: context.window.location,
    tick(time) {now = time;check(source);}};
}
{
  const f = fixture({enabled: false});f.tick(0);
  assert.equal(f.calls.length, 0);assert.equal(f.notices.length, 0);
}
{
  const f = fixture();f.tick(0);
  assert.deepEqual(f.calls, [['hd1080', 'hd1080']]);
  assert.match(f.notices.at(-1), /Requesting/);
  f.tick(250);assert.equal(f.calls.length, 1, 'No per-watchdog request spam');
  f.source.videoWidth = 1920;f.source.videoHeight = 1080;f.tick(500);
  assert.match(f.notices.at(-1), /within/);
  f.tick(750);assert.equal(f.notices.length, 2, 'Deduplicate notices');
  f.source.videoWidth = 3840;f.source.videoHeight = 2160;f.tick(1000);
  assert.equal(f.calls.length, 2, 'Reapply if quality rises');
}
{
  const f = fixture();
  for (const time of [0, 3000, 6000, 9000, 12000]) f.tick(time);
  assert.equal(f.calls.length, 3, 'Bound retries when site ignores requests');
  assert.match(f.notices.at(-1), /Select 1080p/);
  f.location.href = 'https://www.youtube.com/shorts/next';f.tick(13000);
  assert.equal(f.calls.length, 4, 'Retry for in-page navigation');
}
for (const host of ['example.org', 'youtube.com.evil.example']) {
  const f = fixture({host});f.tick(0);
  assert.equal(f.calls.length, 0);assert.match(f.notices.at(-1), /Select 1080p/);
}
{
  const f = fixture({playerMissing: true});f.tick(0);
  assert.match(f.notices.at(-1), /Select 1080p/);
}
{
  const f = fixture();f.player.setPlaybackQualityRange = () => {throw new Error('Unavailable');};
  assert.doesNotThrow(() => f.tick(0));assert.match(f.notices.at(-1), /Select 1080p/);
}
{
  const f = fixture({available: ['hd2160', 'hd720', 'medium']});f.tick(0);
  assert.deepEqual(f.calls, [['hd720', 'hd720']]);
}
{
  const f = fixture({available: ['hd2160']});f.tick(0);
  assert.equal(f.calls.length, 0);assert.match(f.notices.at(-1), /Select 1080p/);
}
{
  const f = fixture();f.source.videoWidth = 1080;f.source.videoHeight = 1920;f.tick(0);
  assert.equal(f.calls.length, 0, 'Portrait 1080p should remain unchanged');
  f.source.videoWidth = 720;f.source.videoHeight = 1280;f.tick(3000);
  assert.equal(f.calls.length, 0, 'Do not upscale lower quality');
}
{
  const f = fixture();f.source.mediaKeys = {};f.tick(0);
  assert.equal(f.calls.length, 0, 'Do not touch protected playback');
}
console.log('Source quality: opt-in, site isolation, confirmation, retry bounds, navigation, portrait and manual fallback passed.');
