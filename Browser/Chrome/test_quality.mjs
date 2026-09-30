/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
const script = await readFile(new URL('./extension/quality.js', import.meta.url), 'utf8');
let now = 0, messages = [], notices = [], token;
const player = {setAttribute: (_, value) => {token = value;}, getAttribute: () => token,
  removeAttribute: () => {token = null;}};
const source = {isConnected: true, ended: false, readyState: 3, mediaKeys: null,
  videoWidth: 3840, videoHeight: 2160, closest: () => player};
const context = {window: {location: {href: 'https://www.youtube.com/watch?v=test', hostname: 'www.youtube.com'}},
  performance: {now: () => now}, crypto: {randomUUID: () => 'test-token'},
  rendepthBrowser: {runtime: {sendMessage: async message => {messages.push(message);return {ok: true};}}}};
vm.runInNewContext(script, context);
const flush = () => new Promise(setImmediate);
context.rendepthCreateQualityLimiter(false, value => notices.push(value))(source);
assert.equal(messages.length, 0);
const limit = context.rendepthCreateQualityLimiter(true, value => notices.push(value));
limit(source);await flush();
assert.equal(messages[0].action, 'quality-limit');assert.equal(messages[0].token, 'test-token');
assert.equal(token, null, 'Remove the temporary page marker after the operation');
assert.match(notices.at(-1), /Requesting/);
for (let i = 0; i < 5; i++) {now += 3100;limit(source);await flush();}
assert.equal(messages.length, 3);assert.match(notices.at(-1), /unavailable or was ignored/);
source.videoWidth = 1920; source.videoHeight = 1080;limit(source);
assert.equal(notices.at(-1), 'Source is within the 1080p limit.');
source.videoWidth = 3840;source.videoHeight = 2160;
context.window.location.hostname = 'youtube.com.example.org';context.window.location.href = 'https://youtube.com.example.org/';
const before = messages.length;limit(source);await flush();assert.equal(messages.length, before);
console.log('Chrome quality: opt-in, fixed page-operation request, marker cleanup, retries, confirmed dimensions and site isolation passed.');
