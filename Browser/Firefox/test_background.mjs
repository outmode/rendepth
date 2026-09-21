/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
const script = await readFile(new URL('./extension/background.js', import.meta.url), 'utf8');
let receive, connect, menuClick, updated, removed;
const sent = [], executed = [], natives = [];
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
const context = {URL, browser: {
  storage: {local: {get: async () => saved, set: async value => Object.assign(saved, value)}},
  permissions: {request: async () => {++permissionRequests;return true;},
    contains: async () => granted, remove: async () => {granted = false;return true;}},
  browserAction: {setBadgeText: noop, setBadgeBackgroundColor: noop, setBadgeTextColor: noop},
  tabs: {executeScript: async (id, options) => executed.push(options.file),
    sendMessage: async (id, message) => {sent.push(message);return {ok: true};},
    query: async () => [{id: 7, url: 'https://example.org/video'}],
    onUpdated: {addListener(fn) {updated = fn;}}, onRemoved: {addListener(fn) {removed = fn;}}},
  runtime: {onMessage: {addListener: fn => {receive = fn;}},
    onConnect: {addListener: fn => {connect = fn;}}, connectNative: () => {
      const native = port();natives.push(native);return native;
    }},
  menus: {create: noop, onClicked: {addListener: fn => {menuClick = fn;}}}
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
assert.equal(permissionRequests, 1, 'Only explicit opt-in may request site access');
console.log('Background: preferences, context menu, navigation/resume, stale ports, origin bounds, tab close and explicit Stop passed.');

// Permission completion must save without any popup continuation or message.
let finishPermission;
context.browser.permissions.request = () => new Promise(resolve => {finishPermission = resolve;});
void context.setAutoReconnect('https://example.org', true);
assert.equal(typeof finishPermission, 'function', 'Request must start synchronously in the user gesture');
assert.equal(saved.autoReconnectOrigins.length, 0);
granted = true;
finishPermission(true);
await flush();
assert.ok(saved.autoReconnectOrigins.includes('https://example.org'));
assert.equal((await receive({action: 'site-status', origin: 'https://example.org'})).autoReconnect, true);
console.log('Delayed permission: background saves opt-in without a surviving popup.');
