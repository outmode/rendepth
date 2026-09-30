/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
const script = await readFile(new URL('./extension/background.js', import.meta.url), 'utf8');
let receive, connect, menuClick, updated, removed;
const sent = [], executed = [], natives = [], menus = [], images = [];
const saved = {limitSource: true};
const noop = () => {};
let granted = true;
let permissionRequests = 0;
function port(extra = {}) {
  let onMessage, onDisconnect;
  return {...extra, messages: [], closed: false,
    postMessage(message) {this.messages.push(message);},
    disconnect() {if (!this.closed) {this.closed = true;onDisconnect?.();}},
    onMessage: {addListener(fn) {onMessage = fn;}},
    onDisconnect: {addListener(fn) {onDisconnect = fn;}},
    emit: message => onMessage(message)};
}
let now = Date.now();
const retries = new Map();
let nextTimer = 0, captureReply = {ok: true}, tabURL = 'https://example.org/video';
const runRetry = async () => {
  assert.equal(retries.size, 1, 'Exactly one recovery timer');
  const [id, callback] = [...retries][0]; retries.delete(id);
  await callback();
};
const context = {URL, AbortController,
  setTimeout(fn, delay) {
    if (delay !== 1000) return setTimeout(fn, delay);
    const id = 'retry-' + ++nextTimer; retries.set(id, fn); return id;
  },
  clearTimeout(id) {retries.delete(id);clearTimeout(id);}, Date: {now: () => now},
  openWebImage: async (...args) => {images.push(args);}, rendepthBrowser: {
  storage: {local: {get: async () => saved, set: async value => Object.assign(saved, value)}},
  permissions: {request: async () => {++permissionRequests;return true;},
    contains: async () => granted, remove: async () => {granted = false;return true;}},
  browserAction: {setBadgeText: noop, setBadgeBackgroundColor: noop, setBadgeTextColor: noop},
  tabs: {executeScript: async (id, options) => executed.push(options.file),
    sendMessage: async (id, message) => {sent.push(message);return captureReply;},
    query: async () => [{id: 7, url: tabURL}],
    onUpdated: {addListener(fn) {updated = fn;}}, onRemoved: {addListener(fn) {removed = fn;}}},
  runtime: {onMessage: {addListener: fn => {receive = fn;}},
    onConnect: {addListener: fn => {connect = fn;}}, connectNative: () => {
      const native = port();natives.push(native);return native;
    }},
  menus: {create: item => menus.push(item), onClicked: {addListener: fn => {menuClick = fn;}}}
}};
vm.runInNewContext(script, context);
const flush = async () => {for (let i = 0; i < 8; ++i) await new Promise(setImmediate);};
function attach(message = sent.at(-1)) {
  const content = port({name: 'rendepth-video', sender: {tab: {id: 7}}});
  connect(content);
  content.emit({action: 'hello', sessionId: message.sessionId, generation: message.generation});
  return content;
}
const open = () => receive({action: 'open', format: '2d'});
assert.deepEqual(menus.filter(item => item.parentId === 'open-image').map(item => item.title),
  ['2D Photo', 'Cross-Eye', 'Parallel']);
for (const [menuItemId, format, swap] of [['image-2d', '2d', false], ['image-cross-eye', 'sbs-full', true], ['image-parallel', 'sbs-full', false]]) {
  menuClick({parentMenuItemId: 'open-image', menuItemId, srcUrl: 'https://images.example/photo.jpg'}, {id: 7});
  await flush();
  assert.equal(images.at(-1)[2], format);
  assert.equal(images.at(-1)[5], swap);
  assert.equal((await receive({action: 'status'})).status, 'Image sent to Rendepth.');
}
now += 5001;
assert.equal((await receive({action: 'status'})).status, 'Choose a playing video.', 'Photo confirmation expires');
assert.equal((await receive({action: 'status'})).limitSource, true);
await open();
assert.equal(sent.at(-1).limitSource, true);
assert.deepEqual(executed, ['quality.js', 'capture.js']);
await receive({action: 'preferences', limitSource: false});
assert.equal(saved.limitSource, false);
await open();
assert.equal(sent.at(-1).limitSource, false);
menuClick({parentMenuItemId: 'open', menuItemId: 'sbs-full', frameId: 0, pageUrl: 'https://example.org/video'}, {id: 7});
await flush();
assert.equal(sent.at(-1).limitSource, false, 'Context menu uses remembered preference');
await open();
const originalMessage = sent.at(-1);
const first = attach();
const native = natives.at(-1);
first.emit({action: 'start', format: '2d'});
const oldRequestId = native.messages.at(-1).requestId;
first.emit({action: 'quality', text: 'Select 1080p manually.'});
first.emit({action: 'connected'});
assert.equal((await receive({action: 'status'})).qualityNotice, 'Select 1080p manually.');
assert.equal(native.messages.length, 1, 'Quality notices must not enter native signalling');
first.emit({action: 'navigate'});
assert.equal(native.closed, false, 'Navigation must retain the native session');
assert.equal(native.messages.at(-1).action, 'navigate');
assert.equal((await receive({action: 'status'})).active, true);
const beforeNavigation = executed.length;
updated(7, {status: 'loading'}, {});
updated(7, {status: 'complete'}, {url: 'https://example.org/next'});
await flush();
assert.equal(executed.length, beforeNavigation, 'Old permission grants alone must not enable auto reconnect');
assert.equal(native.closed, false, 'Default manual navigation keeps the viewer open');
await open();
assert.equal(sent.at(-1).action, 'resume', 'Manual Refresh must wait for playback on paused pages');
assert.equal(sent.at(-1).sessionId, originalMessage.sessionId);
const replacementMessage = sent.at(-1);
const stale = attach(originalMessage);
assert.equal(stale.closed, true, 'Reject ports from an unloaded document');
const next = attach(replacementMessage);
next.emit({action: 'start', format: '2d'});
assert.equal(natives.at(-1), native, 'Reuse the native port for the replacement offer');
native.emit({action: 'answer', requestId: oldRequestId, sdp: 'old'});
assert.equal(next.messages.length, 0, 'Drop a delayed answer from the previous document');
native.emit({action: 'answer', requestId: native.messages.at(-1).requestId, sdp: 'new'});
assert.equal(next.messages.at(-1).sdp, 'new');
first.disconnect();
assert.equal(native.closed, false, 'Old-port disconnect must not end the replacement');
next.emit({action: 'navigate'});
assert.equal((await context.setAutoReconnect('https://example.org', true)).autoReconnect, true);
assert.ok(saved.autoReconnectOrigins.includes('https://example.org'));
updated(7, {status: 'complete'}, {url: 'https://example.org/automatic'});
await flush();
assert.equal(sent.at(-1).action, 'resume');
const automatic = attach();
automatic.emit({action: 'start', format: '2d'});
automatic.emit({action: 'navigate'});
const count = executed.length;
updated(7, {status: 'complete'}, {url: 'https://another.example/video'});
await flush();
assert.equal(executed.length, count, 'Never auto-capture another origin');
assert.equal(native.closed, false, 'Keep viewer waiting even without site access');
await context.setAutoReconnect('https://example.org', false);
assert.equal(granted, false, 'Disabling automatic reconnect removes site access');
assert.equal(saved.autoReconnectOrigins.length, 0);
assert.equal(native.closed, false);
removed(99);assert.equal(native.closed, false);
removed(7);assert.equal(native.closed, true, 'Closing the capture tab must stop');
await open();
const last = attach();
last.emit({action: 'start'});last.emit({action: 'navigate'});
const lastNative = natives.at(-1);
await receive({action: 'stop'});
assert.equal(lastNative.closed, true, 'Explicit Stop must close even while navigating');
assert.equal(sent.at(-1).action, 'cancel', 'Cancel a paused-page resume watcher');
assert.equal((await receive({action: 'status'})).qualityNotice, '');
updated(7, {status: 'complete'}, {url: 'https://example.org/late'});
await flush();
assert.equal((await receive({action: 'status'})).active, false, 'Late navigation cannot resurrect capture');
assert.equal(permissionRequests, 4, 'Each web image requests access; video only requests on reconnect opt-in');
console.log('Background: preferences, context menu, navigation/resume, stale ports, origin bounds, tab close and explicit Stop passed.');

// Permission completion must save without any popup continuation or message.
let finishPermission;
context.rendepthBrowser.permissions.request = () => new Promise(resolve => {finishPermission = resolve;});
void context.setAutoReconnect('https://example.org', true);
assert.equal(typeof finishPermission, 'function', 'Request must start synchronously in the user gesture');
assert.equal(saved.autoReconnectOrigins.length, 0);
granted = true;
finishPermission(true);
await flush();
assert.ok(saved.autoReconnectOrigins.includes('https://example.org'));
assert.equal((await receive({action: 'site-status', origin: 'https://example.org'})).autoReconnect, true);
console.log('Delayed permission: background saves opt-in without a surviving popup.');


let requestedOrigin, resolveImagePermission;
context.rendepthBrowser.permissions.request = permission => {
  requestedOrigin = permission.origins[0];
  return new Promise(resolve => {resolveImagePermission = resolve;});
};
context.openWebImage = async (...args) => {
  if (!await args[3]) throw new Error('Image access was declined.');
  images.push(args);
};
const beforeImages = images.length;
const deniedImage = context.startImage({srcUrl: 'https://cdn.example/photo.png', menuItemId: 'image-cross-eye'}, {id: 7});
assert.equal(requestedOrigin, 'https://cdn.example/*', 'Permission request must start in the menu gesture');
resolveImagePermission(false);
await deniedImage;
assert.equal(images.length, beforeImages);
assert.match((await receive({action: 'status'})).status, /declined/);
const acceptedImage = context.startImage({srcUrl: 'https://cdn.example/photo.png', menuItemId: 'image-cross-eye'}, {id: 7});
resolveImagePermission(true);
await acceptedImage;
assert.equal(images.at(-1)[2], 'sbs-full');
assert.equal(images.at(-1)[5], true);
requestedOrigin = null;
await context.startImage({srcUrl: 'data:image/png;base64,AAAA', menuItemId: 'image-2d'}, {id: 7});
assert.equal(requestedOrigin, null, 'Embedded image must not request host access');
now += 5001;
await receive({action: 'stop'});
assert.equal((await receive({action: 'status'})).status, 'Stopped.', 'Photo expiry cannot overwrite newer status');
console.log('Image permissions: synchronous host request, denial, approval, eye order and embedded image passed.');

await open();
const blockedContent = attach();
const blockedNative = natives.at(-1);
blockedContent.emit({action: 'start', format: '2d'});
blockedContent.emit({action: 'capture-blocked', error: 'Chrome blocked capture. Try Refresh Video.'});
assert.equal(blockedNative.closed, false, 'Blocked replacement retains the native viewer');
assert.equal(blockedNative.messages.at(-1).action, 'navigate');
assert.equal((await receive({action: 'status'})).active, true);
assert.match((await receive({action: 'status'})).status, /every 1 second/);
captureReply = {ok: false, retryable: true, error: 'Still an ad'};
for (let n = 0; n < 6; ++n) await runRetry();
assert.equal(blockedNative.closed, false, 'Keep viewer beyond the former ten-second cutoff');
captureReply = {ok: true};
await runRetry();
const recovered = attach();
recovered.emit({action: 'start', format: '2d'});
assert.equal(retries.size, 0);
assert.equal(natives.at(-1), blockedNative, 'Recovery reuses the same native connection');
recovered.emit({action: 'capture-blocked'});
await receive({action: 'stop'});
assert.equal(retries.size, 0, 'Stop cancels recovery');
await open();
const offsite = attach();offsite.emit({action: 'start', format: '2d'});
offsite.emit({action: 'capture-blocked'});
const injectionCount = executed.length;
tabURL = 'https://other.example/video';
await runRetry();
assert.equal(executed.length, injectionCount, 'Never retry into another origin');
assert.equal(retries.size, 0);
tabURL = 'https://example.org/video';
await receive({action: 'stop'});
await open();
const raceContent = attach();raceContent.emit({action: 'start', format: '2d'});
raceContent.emit({action: 'capture-blocked'});
const originalQuery = context.rendepthBrowser.tabs.query;
let finishQuery;
context.rendepthBrowser.tabs.query = () => new Promise(resolve => {finishQuery = resolve;});
const pendingRetry = runRetry();
const beforeStaleRetry = executed.length;
updated(7, {status: 'loading'}, {url: tabURL});
finishQuery([{id: 7, url: tabURL}]);
await pendingRetry;
assert.equal(executed.length, beforeStaleRetry, 'Navigation cancels a retry already awaiting tab lookup');
context.rendepthBrowser.tabs.query = originalQuery;
await receive({action: 'stop'});
console.log('Recovery: one-second retry, long ads, native reuse, Stop, origin bounds and stale retry cancellation passed.');

// MV3 can restart between opening an image and reopening the popup.
const restartTime = now;
for (const [stored, expected] of [
  [{status: 'Image opened in Rendepth.'}, 'Choose a playing video.'],
  [{status: 'Image sent to Rendepth.', statusExpiresAt: now - 1}, 'Choose a playing video.'],
  [{status: 'Image sent to Rendepth.', statusExpiresAt: now + 5000}, 'Image sent to Rendepth.']
]) {
  now = restartTime;
  const coldContext = {...context, rendepthBrowser: {...context.rendepthBrowser,
    storage: {...context.rendepthBrowser.storage,
      session: {get: async () => stored, set: async value => Object.assign(stored, value)}}}};
  vm.runInNewContext(script, coldContext);
  assert.equal((await receive({action: 'status'})).status, expected);
  now += 5001;
  assert.equal((await receive({action: 'status'})).status, 'Choose a playing video.');
}
console.log('Photo status: expiry survives worker restart and clears legacy confirmations.');
