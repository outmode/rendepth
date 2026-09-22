/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const script = await readFile(new URL('./extension/images.js', import.meta.url), 'utf8');
let width = 3840, height = 1080, denied = false, closed = 0, nativeError = null;
const messages = [], canvases = [], requests = [];
const payload = new Uint8Array(100000).map((_, i) => i % 251);
const context = {URL, Blob, Uint8Array, Promise, Math, String, Error, setTimeout, clearTimeout,
  btoa: value => Buffer.from(value, 'binary').toString('base64'),
  fetch: async (url, options) => {
    requests.push({url, options});
    return new Response(new Blob([payload], {type: 'image/png'}), {status: denied ? 403 : 200});
  },
  createImageBitmap: async () => ({width, height, close() {closed++;}}),
  OffscreenCanvas: class {constructor(width, height) {
    const canvas = {getContext: () => ({drawImage() {}}),
      width, height, convertToBlob: async () => new Blob([payload], {type: 'image/png'})};
    canvases.push(canvas);return canvas;
  }},
  rendepthBrowser: {runtime: {connectNative: () => {
    let receive, disconnect;
    return {
      onMessage: {addListener(fn) {receive = fn;}},
      onDisconnect: {addListener(fn) {disconnect = fn;}},
      disconnect() {disconnect?.();},
      postMessage(message) {
        messages.push(message);
        queueMicrotask(() => receive(nativeError ? {error: nativeError} : {
          action: message.action === 'image-end' ? 'image-opened' : 'image-ready'}));
      }
    };
  }}}
};
vm.runInNewContext(script, context);
const open = (format = 'sbs-full', permission = true, signal = new AbortController().signal) =>
  context.openWebImage({srcUrl: 'https://images.example/full.png'}, {id: 1}, format,
    Promise.resolve(permission), signal);

await open();
assert.equal(canvases[0].width, 3840, 'Do not halve full SBS or scale to video limits');
assert.equal(canvases[0].height, 1080);
assert.equal(messages[0].format, 'sbs-full');
assert.equal(messages[0].swap, false);
assert.equal(messages[0].size, payload.length);
assert.equal(messages.at(-1).action, 'image-end');
const decoded = Buffer.concat(messages.filter(m => m.action === 'image-chunk').map(m => Buffer.from(m.data, 'base64')));
assert.deepEqual(decoded, Buffer.from(payload), 'Chunk transfer must preserve every PNG byte');
assert.ok(messages.every(m => JSON.stringify(m).length < 128 * 1024));
assert.equal(requests[0].options.credentials, 'include');
assert.equal(closed, 1);
messages.length = 0;
await context.openWebImage({srcUrl: 'https://images.example/full.png'}, {id: 1}, 'sbs-full',
  Promise.resolve(true), new AbortController().signal, true);
assert.equal(messages[0].swap, true, 'Cross-Eye must send reversed eye order');
assert.equal(messages[0].format, 'sbs-full');
assert.deepEqual(Buffer.concat(messages.filter(m => m.action === 'image-chunk').map(m => Buffer.from(m.data, 'base64'))),
  Buffer.from(payload), 'Eye interpretation must not resample or rewrite the image');
messages.length = 0;
await open('2d');
assert.equal(messages[0].format, '2d');
messages.length = 0;
await open('sbs-half');
assert.equal(messages[0].format, 'sbs-half');
assert.equal(canvases.at(-1).width, 3840, 'Half SBS pixels stay intact; Rendepth corrects their aspect ratio');
messages.length = 0;
await assert.rejects(open('2d', false), /declined/);
assert.equal(messages.length, 0);
width = 3839;
await assert.rejects(open(), /even image width/);
await assert.rejects(open('sbs-half'), /even image width/);
width = 8192; height = 8192;
await assert.rejects(open('2d'), /dimensions/);
assert.equal(messages.length, 0);
width = 3840; height = 1080; denied = true;
await assert.rejects(open(), /HTTP 403/);
denied = false; nativeError = 'Rendepth is busy';
await assert.rejects(open(), /busy/);
nativeError = null;
const cancelled = new AbortController();cancelled.abort();
await assert.rejects(open('2d', true, cancelled.signal), /abort/i);
console.log('Images: full resolution, exact chunk transfer, formats, permission denial, limits, HTTP/native errors and cancellation passed.');

const normalFetch = context.fetch;
context.chrome = {scripting: {executeScript: async () => [{result: 'data:image/png;base64,AAAA'}]}};
requests.length = 0;
await context.openWebImage({srcUrl: 'https://images.example/photo.png', targetElementId: 42},
  {id: 1}, '2d', Promise.resolve(true), new AbortController().signal);
assert.equal(requests.length, 1);
assert.ok(requests[0].url.startsWith('data:'), 'Readable selected image must avoid a remote fetch');
context.chrome.scripting.executeScript = async () => {throw new Error('Tainted canvas');};
requests.length = 0;
await context.openWebImage({srcUrl: 'https://images.example/photo.png', targetElementId: 42},
  {id: 1}, '2d', Promise.resolve(true), new AbortController().signal);
assert.equal(requests[0].url, 'https://images.example/photo.png', 'Fall back to fetch without a permission request');
context.fetch = async () => {throw new TypeError('NetworkError');};
await assert.rejects(open(), /NetworkError/);
context.fetch = normalFetch;
denied = true;
await assert.rejects(open(), error => !error.needsImageAccess && /HTTP 403/.test(error.message));
console.log('Image reading: page snapshot, fetch fallback, network and HTTP errors passed.');
