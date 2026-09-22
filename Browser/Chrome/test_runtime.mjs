/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
// Real Chrome MV3 smoke/integration checks in a disposable profile, via CDP pipe.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import {mkdtemp, rm, readFile, writeFile, mkdir, cp, access} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
const temporary = await mkdtemp(join(tmpdir(), 'rendepth-chrome-test-'));
await mkdir(join(temporary, 'runtime'), {mode: 0o700});
await cp(join(root, 'extension'), join(temporary, 'extension'), {recursive: true});
const manifest = JSON.parse(await readFile(join(temporary, 'extension/manifest.json')));
// Test fixture grant only; shipping manifest has optional host access only.
manifest.host_permissions = ['http://127.0.0.1/*'];
await writeFile(join(temporary, 'extension/manifest.json'), JSON.stringify(manifest));
await writeFile(join(temporary, 'mode'), 'video');
execFileSync('python3', [join(root, 'native/install.py'), '--rendepth', '/bin/false',
  '--directory', join(temporary, 'profile/NativeMessagingHosts')]);
const bridge = spawn('python3', [join(root, 'test_bridge.py'), temporary,
  resolve(root, '../../Debug/BrowserCaptureTest')], {stdio: ['ignore', 'pipe', 'inherit']});
const bridgeEvents = [];
let bridgeBuffer = '';
bridge.stdout.on('data', bytes => {
  bridgeBuffer += bytes;
  let end;
  while ((end = bridgeBuffer.indexOf('\n')) >= 0) {
    bridgeEvents.push(JSON.parse(bridgeBuffer.slice(0, end)));bridgeBuffer = bridgeBuffer.slice(end + 1);
  }
});
execFileSync('ffmpeg', ['-v', 'error', '-f', 'lavfi', '-i',
  'color=c=red:size=640x360:rate=30:duration=20', '-vf', 'drawbox=x=320:y=0:w=320:h=360:color=blue:t=fill',
  '-c:v', 'libvpx', '-threads', '4', '-b:v', '1M', join(temporary, 'video.webm')]);
execFileSync('python3', ['-c', `from PIL import Image
im=Image.new('RGB',(3840,1080),'red'); im.paste('blue',(1920,0,3840,1080)); im.save(${JSON.stringify(join(temporary, 'image.png'))})`]);
const server = createServer(async (req, res) => {
  if (req.url === '/video.webm' || req.url === '/image.png') {
    const bytes = await readFile(join(temporary, req.url.slice(1)));
    res.writeHead(200, {'Content-Type': req.url.endsWith('png') ? 'image/png' : 'video/webm', 'Content-Length': bytes.length});
    res.end(bytes);
  } else {
    res.setHeader('Content-Type', 'text/html');
    res.end('<html><body><video width="640" height="360" muted loop controls src="/video.webm"></video><img src="/image.png" width="384"></body></html>');
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = 'http://127.0.0.1:' + server.address().port;
const chrome = spawn(process.env.CHROME_BINARY || 'google-chrome', [
  '--headless=new', '--no-first-run', '--no-default-browser-check',
  '--enable-unsafe-extension-debugging', '--remote-debugging-pipe',
  '--user-data-dir=' + join(temporary, 'profile'), 'about:blank'
], {env: {...process.env, XDG_RUNTIME_DIR: join(temporary, 'runtime')},
  stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe']});
let errors = '', serial = 0, buffer = '';
const pending = new Map();
chrome.stderr.on('data', bytes => {errors = (errors + bytes).slice(-12000);});
chrome.stdio[4].on('data', bytes => {
  buffer += bytes;
  for (;;) {
    const end = buffer.indexOf('\0');
    if (end < 0) break;
    const message = JSON.parse(buffer.slice(0, end));buffer = buffer.slice(end + 1);
    const waiter = pending.get(message.id);
    if (waiter) {
      pending.delete(message.id);clearTimeout(waiter.timer);
      if (message.error) waiter.reject(new Error(JSON.stringify(message.error)));
      else waiter.resolve(message.result);
    }
  }
});
function call(method, params = {}, sessionId) {
  return new Promise((resolve, reject) => {
    const id = ++serial;
    const timer = setTimeout(() => {pending.delete(id);reject(new Error('CDP timeout: ' + method + '\n' + errors));}, 30000);
    pending.set(id, {resolve, reject, timer});
    chrome.stdio[3].write(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}) + '\0');
  });
}
async function evaluate(sessionId, expression) {
  const reply = await call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true, userGesture: true}, sessionId);
  if (reply.exceptionDetails) throw new Error(JSON.stringify(reply.exceptionDetails));
  return reply.result.value;
}
async function waitFor(fn, timeout = 20000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const result = await fn();if (result) return result;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error('Timed out waiting for Chrome test state\n' + errors);
}
try {
  await waitFor(() => bridgeEvents.some(event => event.event === 'ready'));
  const {id} = await call('Extensions.loadUnpacked', {path: join(temporary, 'extension')});
  console.log('Loaded Chrome extension:', id);
  const worker = await waitFor(async () => (await call('Target.getTargets')).targetInfos.find(t =>
    t.type === 'service_worker' && t.url.startsWith('chrome-extension://' + id)));
  let {sessionId} = await call('Target.attachToTarget', {targetId: worker.targetId, flatten: true});
  assert.equal(await evaluate(sessionId, 'typeof startImage'), 'function');
  assert.equal(await evaluate(sessionId, 'typeof OffscreenCanvas'), 'function');
  const popup = await call('Target.createTarget', {url: `chrome-extension://${id}/popup.html`});
  const {sessionId: popupSession} = await call('Target.attachToTarget', {targetId: popup.targetId, flatten: true});
  await waitFor(() => evaluate(popupSession, 'document.readyState === "complete" && !!document.querySelector("h1")'));
  assert.equal(await evaluate(popupSession, 'document.querySelector("h1").textContent'), 'Open in Rendepth');
  assert.deepEqual(await evaluate(popupSession, '[...document.querySelector("#format").options].map(o => o.textContent)'),
    ['2D Video', 'SBS Half', 'SBS Full']);
  assert.equal(await evaluate(popupSession, 'document.querySelector("#image-access")'), null);
  console.log('Chrome MV3 worker and production popup loaded successfully.');
  const page = await call('Target.createTarget', {url: origin});
  const {sessionId: pageSession} = await call('Target.attachToTarget', {targetId: page.targetId, flatten: true});
  await waitFor(() => evaluate(pageSession, '!!document.querySelector("img")?.complete && document.querySelector("video")?.readyState >= 2'));
  const tabId = await evaluate(sessionId, `(async () => (await chrome.tabs.query({})).find(t => t.url === ${JSON.stringify(origin + '/')}).id)()`);
  // CDP worker evaluation does not carry a browser context-menu gesture. The
  // fixture host is already granted in its test-only manifest; replace only
  // the prompt operation with a check of that real Chrome permission.
  await evaluate(sessionId, 'rendepthBrowser.permissions = {...chrome.permissions, request: options => chrome.permissions.contains(options)}');
  for (const [menuItemId, format, swap] of [['image-2d', '2d', false], ['image-cross-eye', 'sbs-full', true], ['image-parallel', 'sbs-full', false]]) {
    const before = bridgeEvents.length;
    await evaluate(sessionId, `startImage(${JSON.stringify({srcUrl: origin + '/image.png', menuItemId, frameId: 0, targetElementId: origin + '/image.png'})}, {id:${tabId}})`);
    const request = bridgeEvents.slice(before).find(event => event.event === 'request');
    assert.equal(request.format, format);assert.equal(request.swap, swap);
    const image = bridgeEvents.slice(before).find(event => event.event === 'image');
    assert.deepEqual(image.size, [3840, 1080]);assert.deepEqual(image.left, [255, 0, 0]);assert.deepEqual(image.right, [0, 0, 255]);
  }
  console.log('Chrome photos: production native messaging, 2D/Cross-Eye/Parallel and exact full-resolution eye pixels passed.');
  await evaluate(pageSession, 'document.querySelector("video").play()');
  for (const [format, scaleHalf, expectedFormat, expectedWidth] of [['2d', false, '2d', 640],
    ['sbs-full', false, 'sbs-full', 640], ['sbs-half', false, 'sbs-half', 640], ['sbs-half', true, 'sbs-full', 1280]]) {
    const before = bridgeEvents.length;
    await evaluate(sessionId, `start(${tabId}, 0, ${JSON.stringify(origin + '/video.webm')}, ${JSON.stringify(format)}, ${scaleHalf}, ${JSON.stringify(origin)})`);
    const result = await waitFor(() => bridgeEvents.slice(before).find(event => event.event === 'video-result'), 45000);
    assert.equal(result.code, 0, result.error);
    const stats = JSON.parse(result.output.trim());
    assert.equal(stats.stereo, true);assert.equal(stats.width, expectedWidth);assert.ok(stats.frames > 0);
    assert.equal(bridgeEvents.slice(before).find(event => event.event === 'request').format, expectedFormat);
    console.log('Chrome video:', format, 'expand:', scaleHalf, stats);
    await evaluate(sessionId, 'stop()');
  }

  // Simulate a player announcing an eleven-second ad on the same video element.
  // Use the real isolated capture world, native host and receiver throughout.
  await writeFile(join(temporary, 'mode'), 'navigation');
  const beforeRecovery = bridgeEvents.length;
  await evaluate(sessionId, `start(${tabId}, 0, null, 'sbs-full', false, ${JSON.stringify(origin)})`);
  const recoveryRequest = await waitFor(() => bridgeEvents.slice(beforeRecovery).find(event => event.event === 'request'));
  const markerExists = path => access(path).then(() => true, () => false);
  await waitFor(() => markerExists(join(recoveryRequest.directory, 'test-first-frame')));
  await evaluate(sessionId, `chrome.scripting.executeScript({target: {tabId: ${tabId}}, func: () => {
    const video = document.querySelector('video');
    video.setAttribute('data-is-ad', 'true');
    setTimeout(() => video.removeAttribute('data-is-ad'), 11000);
  }})`);
  await waitFor(() => markerExists(join(recoveryRequest.directory, 'test-resumed-frame')), 25000);
  assert.equal(bridgeEvents.slice(beforeRecovery).filter(event => event.event === 'request').length, 1);
  await evaluate(sessionId, 'stop()');
  const recovery = await waitFor(() => bridgeEvents.slice(beforeRecovery).find(event => event.event === 'video-result'));
  assert.equal(recovery.code, 0, recovery.error);
  console.log('Chrome recovery: observed ad state, held the original receiver, and resumed after 11 seconds without controlling page playback.');

  await writeFile(join(temporary, 'mode'), 'navigation');
  const beforeNavigation = bridgeEvents.length;
  await evaluate(sessionId, `start(${tabId}, 0, null, 'sbs-full', false, ${JSON.stringify(origin)})`);
  const request = await waitFor(() => bridgeEvents.slice(beforeNavigation).find(event => event.event === 'request'));
  const exists = path => access(path).then(() => true, () => false);
  await waitFor(() => exists(join(request.directory, 'test-first-frame')));
  await call('Page.navigate', {url: origin + '/next'}, pageSession);
  await waitFor(async () => (await readFile(join(request.directory, 'state'), 'utf8')).trim() === 'waiting');
  // Detach the worker debugger: otherwise CDP itself would keep it alive.
  await call('Target.closeTarget', {targetId: popup.targetId});
  await call('Target.detachFromTarget', {sessionId});
  console.log('Chrome navigation: checking a 32-second paused interval without a worker debugger attached.');
  await new Promise(resolve => setTimeout(resolve, 32000));
  const retainedWorker = (await call('Target.getTargets')).targetInfos.find(t => t.targetId === worker.targetId);
  assert.ok(retainedWorker, 'Native messaging must retain the MV3 worker past its normal idle timeout');
  ({sessionId} = await call('Target.attachToTarget', {targetId: worker.targetId, flatten: true}));
  assert.equal(await evaluate(sessionId, 'session?.navigating'), true);
  await evaluate(sessionId, `void start(${tabId}, 0, null, 'sbs-full', false, ${JSON.stringify(origin)}); true`);
  await evaluate(pageSession, 'document.querySelector("video").play()');
  await waitFor(() => exists(join(request.directory, 'test-resumed-frame')));
  assert.equal(bridgeEvents.slice(beforeNavigation).filter(event => event.event === 'request').length, 1);
  await evaluate(sessionId, 'stop()');
  const navigation = await waitFor(() => bridgeEvents.slice(beforeNavigation).find(event => event.event === 'video-result'));
  assert.equal(navigation.code, 0, navigation.error);
  console.log('Chrome navigation:', navigation.output.trim());

} finally {
  try {await call('Browser.close');} catch {}
  chrome.kill();
  for (const waiter of pending.values()) clearTimeout(waiter.timer);
  await new Promise(resolve => chrome.exitCode != null ? resolve() : chrome.once('exit', resolve));
  bridge.kill('SIGINT');
  await new Promise(resolve => bridge.exitCode != null ? resolve() : bridge.once('exit', resolve));
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
  await rm(temporary, {recursive: true, force: true});
}
