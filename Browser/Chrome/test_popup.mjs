/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
const script = await readFile(new URL('./extension/popup.js', import.meta.url), 'utf8');
const elements = new Map(), messages = [], requests = [];
let grant = true, autoReconnect = false;
function element() {
  return {checked: false, disabled: true, value: '', hidden: false, children: [],
    dataset: {}, setAttribute() {}, addEventListener() {}, appendChild(child) {this.children.push(child);}};
}
function get(id) {
  if (!elements.has(id)) elements.set(id, element());
  return elements.get(id);
}
const format = get('format');
format.options = [{value: '2d', textContent: '2D Video'}, {value: 'sbs-full', textContent: 'SBS Full'}];
format.value = '2d';
Object.defineProperty(format, 'selectedOptions', {get: () => format.options.filter(option => option.value === format.value)});
vm.runInNewContext(script, {URL, setInterval() {}, document: {
  getElementById: get, createElement: element, addEventListener() {}
}, rendepthBrowser: {
  tabs: {query: async () => [{url: 'https://example.org/video'}]},
  extension: {getBackgroundPage: () => ({setAutoReconnect: async (origin, enabled) => {
    if (enabled) requests.push(origin);
    autoReconnect = enabled && grant;
    return {autoReconnect};
  }})},
  runtime: {sendMessage: async message => {
    messages.push(message);
    if (message.action === 'auto-reconnect') {if (message.enabled) requests.push(message.origin); autoReconnect = message.enabled && grant;}
    if (message.action === 'site-status' || message.action === 'auto-reconnect') return {autoReconnect};
    return {active: true, status: 'Waiting', qualityNotice: '', limitSource: false,
      options: {format: 'sbs-full', scaleHalf: false}};
  }}
}});
const flush = async () => {for (let i = 0; i < 8; ++i) await new Promise(setImmediate);};
await flush();
assert.equal(requests.length, 0, 'Opening the popup must not request access');
assert.equal(get('auto-reconnect').checked, false);
await get('open').onclick();
assert.equal(requests.length, 0, 'Open/Refresh must not request access');
const open = messages.find(message => message.action === 'open');
assert.equal(open.format, 'sbs-full', 'Manual Refresh must retain active capture format');
assert.equal(open.scaleHalf, false);
const control = get('auto-reconnect');
control.checked = true;
await control.onchange({target: control});
assert.equal(requests.length, 1);
assert.equal(requests[0], 'https://example.org');
assert.equal(control.checked, true);
control.checked = false;
await control.onchange({target: control});
assert.equal(requests.length, 1, 'Disabling must never request permission');
assert.equal(autoReconnect, false);
grant = false;
control.checked = true;
await control.onchange({target: control});
assert.equal(control.checked, false, 'Denied access must leave auto reconnect off');
assert.match(get('navigation-notice').textContent, /Refresh Video/);
console.log('Popup: no default permission request, explicit site opt-in, denial and capture-format preservation passed.');
