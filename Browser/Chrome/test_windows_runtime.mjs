/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
// Real Chrome MV3 -> registered Windows host -> Rendepth photo/video smoke test.
import assert from 'node:assert/strict';
import {spawn, execFileSync} from 'node:child_process';
import {mkdtemp, rm, readFile, writeFile, cp, mkdir, readdir, stat} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {join, resolve, sep} from 'node:path';
import {fileURLToPath} from 'node:url';
import {deflateSync} from 'node:zlib';

if (process.platform !== 'win32') throw new Error('This smoke test is for Windows.');
const root = fileURLToPath(new URL('.', import.meta.url));
function appProcesses() {
  const result = execFileSync('powershell.exe', ['-NoProfile', '-Command',
    'Get-Process Rendepth -ErrorAction SilentlyContinue | Select-Object Id,MainWindowTitle | ConvertTo-Json -Compress; exit 0'],
    {encoding: 'utf8'}).trim();
  return result ? [JSON.parse(result)].flat() : [];
}
const beforeApps = new Set(appProcesses().map(app => app.Id));
const testStarted = Date.now();
const photoRuntime = join(process.env.APPDATA, 'Outmode/Rendepth/Runtime');
const temporary = await mkdtemp(join(tmpdir(), 'rendepth-chrome-windows-'));
assert.ok(resolve(temporary).startsWith(resolve(tmpdir()) + sep));
const profile = join(temporary, 'profile');
await mkdir(profile);
await cp(join(root, 'extension'), join(temporary, 'extension'), {recursive: true});
const manifest = JSON.parse(await readFile(join(temporary, 'extension/manifest.json')));
manifest.host_permissions = ['http://127.0.0.1/*']; // Test fixture grant only.
await writeFile(join(temporary, 'extension/manifest.json'), JSON.stringify(manifest));

const html = await readFile(resolve(root, '../Firefox/test-video.html'));
const server = createServer((_request, response) => {
  response.writeHead(200, {'Content-Type': 'text/html'});
  response.end(html);
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const origin = `http://127.0.0.1:${server.address().port}`;
const chromeBinary = process.env.CHROME_BINARY || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const chrome = spawn(chromeBinary, ['--headless=new', '--no-first-run', '--no-default-browser-check',
  '--enable-unsafe-extension-debugging', '--remote-debugging-pipe', '--user-data-dir=' + profile,
  'about:blank'], {stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe']});
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
  return new Promise((done, reject) => {
    const id = ++serial;
    const timer = setTimeout(() => {pending.delete(id);reject(new Error('CDP timeout: ' + method + '\n' + errors));}, 30000);
    pending.set(id, {resolve: done, reject, timer});
    chrome.stdio[3].write(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}) + '\0');
  });
}
async function evaluate(sessionId, expression) {
  const reply = await call('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true,
    userGesture: true}, sessionId);
  if (reply.exceptionDetails) throw new Error(JSON.stringify(reply.exceptionDetails));
  return reply.result.value;
}
async function waitFor(check, timeout = 30000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const result = await check();
    if (result) return result;
    await new Promise(done => setTimeout(done, 200));
  }
  throw new Error('Timed out waiting for Chrome or Rendepth. ' + errors);
}
async function captureDirectories() {
  return (await readdir(tmpdir())).filter(name => name.startsWith('rendepth-firefox-'));
}
function chunk(kind, data) {
  const body = Buffer.concat([Buffer.from(kind), data]);
  let crc = 0xffffffff;
  for (const byte of body) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  const result = Buffer.alloc(12 + data.length);
  result.writeUInt32BE(data.length, 0);body.copy(result, 4);
  result.writeUInt32BE((crc ^ 0xffffffff) >>> 0, result.length - 4);
  return result;
}
const header = Buffer.alloc(13);
header.writeUInt32BE(2, 0);header.writeUInt32BE(1, 4);header[8] = 8;header[9] = 2;
const image = Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', header),
  chunk('IDAT', deflateSync(Buffer.from([0, 255, 0, 0, 0, 0, 255]))), chunk('IEND', Buffer.alloc(0))]);

try {
  const {id} = await call('Extensions.loadUnpacked', {path: join(temporary, 'extension')});
  assert.equal(id, 'ocmhnmfbdiamechohheannkbhdpbnjgl');
  const worker = await waitFor(async () => (await call('Target.getTargets')).targetInfos.find(t =>
    t.type === 'service_worker' && t.url.startsWith('chrome-extension://' + id)));
  const {sessionId} = await call('Target.attachToTarget', {targetId: worker.targetId, flatten: true});
  const page = await call('Target.createTarget', {url: origin});
  const {sessionId: pageSession} = await call('Target.attachToTarget', {targetId: page.targetId, flatten: true});
  await waitFor(() => evaluate(pageSession, 'document.readyState === "complete" && !!document.querySelector("video")'));
  const tabId = await evaluate(sessionId, `(async () => (await chrome.tabs.query({})).find(t => t.url === ${JSON.stringify(origin + '/')}).id)()`);

  const imageUrl = `data:image/png;base64,${image.toString('base64')}`;
  await evaluate(sessionId, `startImage(${JSON.stringify({srcUrl: imageUrl, menuItemId: 'image-parallel', frameId: 0})}, {id:${tabId}})`);
  assert.equal(await evaluate(sessionId, 'status'), 'Image sent to Rendepth.');
  const photo = await waitFor(async () => {
    try {
      for (const instance of await readdir(photoRuntime))
        for (const directory of await readdir(join(photoRuntime, instance)))
          if (directory.startsWith('WebPhoto-')) {
            const path = join(photoRuntime, instance, directory, 'Photo_sbs.png');
            try {
              const pixels = await readFile(path);
              if ((await stat(path)).mtimeMs >= testStarted &&
                  pixels.subarray(0, 8).equals(image.subarray(0, 8)) &&
                  pixels.readUInt32BE(16) === 2 && pixels.readUInt32BE(20) === 1) return path;
            } catch {}
          }
    } catch {}
    return null;
  });
  console.log('Chrome stereo photo reached Rendepth at original dimensions:', photo);

  await evaluate(pageSession, 'document.querySelector("#play").click(); true');
  await waitFor(() => evaluate(pageSession, 'document.querySelector("video").videoWidth > 0'));
  // Turn the animated canvas into a looping media file. Chrome can end a
  // captureStream() track when recapturing a video backed by another live stream.
  const recording = await evaluate(pageSession, `(async () => {
    const video = document.querySelector('video');
    const chunks = [];
    const recorder = new MediaRecorder(video.srcObject, {mimeType: 'video/webm;codecs=vp8'});
    recorder.ondataavailable = event => { if (event.data.size) chunks.push(event.data); };
    const finished = new Promise(resolve => recorder.onstop = resolve);
    recorder.start();
    await new Promise(resolve => setTimeout(resolve, 3000));
    recorder.stop();
    await finished;
    video.pause(); video.srcObject = null;
    video.src = URL.createObjectURL(new Blob(chunks, {type: 'video/webm'}));
    video.loop = true;
    try { await video.play(); } catch (error) { return {sizes: chunks.map(chunk => chunk.size), error: error.message, mime: MediaRecorder.isTypeSupported('video/webm;codecs=vp8')}; }
    return {sizes: chunks.map(chunk => chunk.size)};
  })()`);
  assert.ok(recording.sizes.some(size => size > 0), `Could not record video fixture: ${JSON.stringify(recording)}`);
  assert.ok(!recording.error, `Could not play video fixture: ${JSON.stringify(recording)}`);
  await waitFor(() => evaluate(pageSession, 'document.querySelector("video").videoWidth > 0'));
  for (const [format, scaleHalf] of [['sbs-full', false], ['sbs-half', false],
      ['sbs-half', true], ['2d', false]]) {
    const beforeCapture = new Set(await captureDirectories());
    await evaluate(sessionId, `start(${tabId}, 0, null, ${JSON.stringify(format)}, ${scaleHalf}, ${JSON.stringify(origin)})`);
    await waitFor(() => evaluate(sessionId,
      'status === "Streaming to Rendepth. Audio and playback stay in Chrome." ? status : null'), 45000);
    await waitFor(() => appProcesses().some(app => app.MainWindowTitle === 'Rendepth - Browser Video'), 45000);
    console.log('Chrome video connected to Rendepth:', format, 'expand:', scaleHalf);
    if (format === 'sbs-full') {
      await evaluate(sessionId, 'suspend(session)');
      await waitFor(() => evaluate(sessionId,
        'status === "Waiting for the next video. Rendepth stays open." ? status : null'));
      await evaluate(sessionId, `start(${tabId}, 0, null, ${JSON.stringify(format)}, ${scaleHalf}, ${JSON.stringify(origin)})`);
      try {
        await waitFor(() => evaluate(sessionId,
          'status === "Streaming to Rendepth. Audio and playback stay in Chrome." ? status : null'), 45000);
      } catch (error) {
        throw new Error(`Reconnect status: ${await evaluate(sessionId, 'status')}; ${error.message}`);
      }
      console.log('Chrome video reconnected after pause without a state-file error.');
    }
    const activeCaptures = (await captureDirectories()).filter(name => !beforeCapture.has(name));
    assert.equal(activeCaptures.length, 1, 'Expected one live native-host session directory');
    await evaluate(sessionId, 'stop()');
    await waitFor(async () => {
      const remaining = await captureDirectories();
      return activeCaptures.every(name => !remaining.includes(name));
    }, 10000);
  }
} finally {
  try {await call('Browser.close');} catch {}
  chrome.kill();
  for (const waiter of pending.values()) clearTimeout(waiter.timer);
  await new Promise(done => chrome.exitCode != null ? done() : chrome.once('exit', done));
  for (const app of appProcesses()) if (!beforeApps.has(app.Id)) {
    try {process.kill(app.Id);} catch {}
  }
  server.closeAllConnections();
  await new Promise(done => server.close(done));
  await rm(temporary, {recursive: true, force: true});
}
