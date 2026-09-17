/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';

const script = await readFile(new URL('./extension/capture.js', import.meta.url), 'utf8');
const flush = async () => { for (let n = 0; n < 8; ++n) await new Promise(setImmediate); };

async function fixture(format = 'sbs-full', width = 1920, height = 1080, scaleHalf = true) {
  let start, now = 0, interval, disconnect, replacementGate;
  const messages = [], streams = [], configurations = [];
  const makeTrack = () => ({readyState: 'live', muted: false, stop() {this.readyState = 'ended';}});
  class Video extends EventTarget {
    paused = false; videoWidth = 1920; videoHeight = 1080; readyState = 4;
    clientWidth = 960; clientHeight = 540; isConnected = true;
    rect = {left: 0, top: 0, right: 960, bottom: 540, width: 960};
    getBoundingClientRect() {return this.rect;}
    captureStream() {
      const video = makeTrack(), audio = makeTrack();
      let tracks = [video, audio];
      const stream = {getTracks: () => tracks, getVideoTracks: () => [video],
        getAudioTracks: () => tracks.includes(audio) ? [audio] : [],
        removeTrack(track) {tracks = tracks.filter(t => t !== track);}};
      streams.push(stream);
      return stream;
    }
  }
  const source = new Video();
  const videos = [source];
  const testWindow = new EventTarget();
  testWindow.innerWidth = 1920;testWindow.innerHeight = 1080;
  source.videoWidth = width;source.videoHeight = height;
  const callbacks = new Map(), draws = [];
  let callbackId = 0, drawError;
  source.requestVideoFrameCallback = fn => {callbacks.set(++callbackId, fn);return callbackId;};
  source.cancelVideoFrameCallback = id => callbacks.delete(id);
  const canvas = {width: 0, height: 0,
    getContext: () => ({drawImage(...args) {if (drawError) throw drawError;draws.push(args.slice(1));}}),
    captureStream(rate) {
      assert.equal(rate, 60, 'Canvas capture must be capped at 60 FPS');
      const stream = source.captureStream();
      stream.getVideoTracks()[0].requestFrame = () => {};
      return stream;
    }};
  const sender = {
    getParameters() {return {encodings: [{}]};},
    async setParameters(p) {configurations.push(JSON.parse(JSON.stringify(p)));},
    async replaceTrack(t) {if (replacementGate) await replacementGate; this.track = t;}
  };
  const port = {closed: false, onDisconnect: {addListener(fn) {disconnect = fn;}},
    onMessage: {addListener() {}}, postMessage(message) {messages.push(message);},
    disconnect() {this.closed = true;}};
  class Peer {
    iceGatheringState = 'complete'; connectionState = 'connected';
    addTransceiver(track) {sender.track = track; return {sender, setCodecPreferences() {}};}
    async createOffer() {return {type: 'offer', sdp: 'test'};}
    async setLocalDescription(offer) {this.localDescription = offer;}
    close() {this.connectionState = 'closed';}
  }
  vm.runInNewContext(script, {HTMLVideoElement: Video, RTCPeerConnection: Peer,
    RTCRtpSender: {getCapabilities: () => ({codecs: [{mimeType: 'video/VP8'}]})},
    document: {querySelectorAll: () => videos, createElement: () => canvas}, window: testWindow,
    performance: {now: () => now}, setTimeout, clearTimeout,
    setInterval(fn) {interval = fn;return 1;}, clearInterval() {interval = null;},
    browser: {runtime: {onMessage: {addListener(fn) {start = fn;}}, connect: () => port}}});
  const result = await start({action: 'start', format, swap: true, scaleHalf});
  assert.equal(result.ok, true);
  return {source, sender, port, streams, configurations, messages, canvas, callbacks, draws,
    frame() {const pending = [...callbacks.values()];callbacks.clear();pending.forEach(fn => fn());},
    drawFailure() {drawError = new Error('Canvas capture denied');},
    addVideo() {const video = new Video();
      video.requestVideoFrameCallback = source.requestVideoFrameCallback;
      video.cancelVideoFrameCallback = source.cancelVideoFrameCallback;videos.push(video);return video;},
    leavePage: () => testWindow.dispatchEvent(new Event("pagehide")),
    stop: () => disconnect(), gate(promise) {replacementGate = promise;},
    tick(time) {now = time;interval?.();},
    reset() {source.videoWidth = 0;source.readyState = 0;source.dispatchEvent(new Event('emptied'));},
    loaded() {source.videoWidth = 3840;source.videoHeight = 2160;source.readyState = 4;
      source.dispatchEvent(new Event('resize'));source.dispatchEvent(new Event('loadeddata'));}};
}

{
  const f = await fixture('sbs-half', 3840, 2160);
  assert.equal(f.canvas.width, 3840);
  assert.equal(f.canvas.height, 1080);
  assert.equal(f.messages.find(m => m.action === 'start').format, 'sbs-full',
    'The viewer must not stretch the already normalized eyes again');
  assert.equal(f.messages.find(m => m.action === 'start').swap, true);
  assert.equal(f.configurations.at(-1).encodings[0].scaleResolutionDownBy, 1);
  const original = f.sender.track;
  f.reset();f.frame();await flush();
  assert.equal(f.draws.length, 1, 'Buffering must retain the last valid canvas image');
  f.loaded();f.frame();await flush();
  assert.equal(f.sender.track, original, 'A source reset must retain the canvas track');
  assert.equal(f.streams.length, 1);
  f.source.videoWidth = 1920;f.source.videoHeight = 1080;f.frame();
  assert.equal(f.canvas.width, 3840);
  assert.equal(f.canvas.height, 1080, 'A lower-quality source must preserve the same eye proportions');
  f.source.videoWidth = 1280;f.source.videoHeight = 720;f.frame();
  assert.equal(f.canvas.width, 2560);assert.equal(f.canvas.height, 720);
  f.stop();assert.equal(f.callbacks.size, 0, 'Stop must cancel frame processing');
  assert.equal(original.readyState, 'ended');
}
{
  const f = await fixture('sbs-half', 3840, 2160);
  f.drawFailure();f.frame();
  assert.equal(f.port.closed, true);
  assert.equal(f.callbacks.size, 0);
  assert.ok(f.messages.some(m => m.error === 'Canvas capture denied'));
}
{
  const f = await fixture('sbs-half', 3840, 2160);
  f.source.mediaKeys = {};f.frame();
  assert.equal(f.port.closed, true, 'A protected replacement must stop scaling');
}

{
  const f = await fixture();
  const original = f.sender.track;
  f.reset();await flush();
  assert.equal(f.port.closed, false, 'A source reset must not close the peer');
  f.loaded();await flush();
  assert.equal(f.streams.length, 2, 'Capture must attach a replacement track');
  assert.notEqual(f.sender.track, original);
  assert.equal(original.readyState, 'ended');
  assert.equal(f.sender.track.readyState, 'live');
  assert.equal(f.configurations.at(-1).encodings[0].scaleResolutionDownBy, 2);
  assert.equal(f.configurations.at(-1).degradationPreference, 'maintain-resolution');
  f.source.videoHeight = 1080;f.source.dispatchEvent(new Event('resize'));await flush();
  assert.equal(f.configurations.at(-1).encodings[0].scaleResolutionDownBy, 1,
    'Full-width 3840x1080 SBS must retain both eyes at native resolution');
  assert.equal(f.port.closed, false);
  f.source.ended = true;f.source.dispatchEvent(new Event('ended'));await flush();
  assert.equal(f.port.closed, false, 'Playback end must wait for the next video');
  f.leavePage();assert.equal(f.port.closed, true, 'Leaving the page must stop capture');
}
{
  const f = await fixture();
  f.sender.track.muted = true;f.source.readyState = 2;
  f.tick(1000);f.tick(8000);
  assert.equal(f.port.closed, false, 'Buffering must not be mistaken for a capture restriction');
  f.source.readyState = 4;
  f.tick(9000);f.tick(16000);
  assert.equal(f.port.closed, true, 'Persistent unavailable capture with playable source must report an error');
  assert.ok(f.messages.some(m => m.action === 'error'));
}
{
  const f = await fixture();
  let release;
  f.gate(new Promise(resolve => {release = resolve;}));
  f.reset();f.loaded();await flush();
  f.stop();release();await flush();
  assert.ok(f.streams.every(s => s.getTracks().every(t => t.readyState === 'ended')),
    'Stopping during replaceTrack must release the pending stream');
  assert.equal(f.port.closed, true);
}
{
  const f = await fixture();
  f.reset();f.source.mediaKeys = {};f.loaded();await flush();
  assert.equal(f.port.closed, true);
  assert.ok(f.messages.some(m => m.error?.includes('protected')));
  assert.equal(f.streams.length, 1, 'A protected replacement must not be captured');
}
console.log('Capture: source reset/replacement, scaling, buffering, cancellation and protected replacement passed.');

{
  const f = await fixture('sbs-half', 3840, 2160, false);
  assert.equal(f.callbacks.size, 0, 'Fast capture must not process canvas frames');
  assert.equal(f.messages.find(m => m.action === 'start').format, 'sbs-half');
  assert.equal(f.configurations.at(-1).encodings[0].scaleResolutionDownBy, 2);
  f.stop();
}

{
  const f = await fixture('2d', 3840, 2160);
  assert.equal(f.callbacks.size, 0, '2D video must use direct capture');
  assert.equal(f.messages.find(m => m.action === 'start').format, '2d');
  assert.equal(f.configurations.at(-1).encodings[0].scaleResolutionDownBy, 2);
  f.stop();
}

{
  const f = await fixture('2d');
  const old = f.sender.track;
  f.source.isConnected = false;f.tick(1000);await flush();
  assert.equal(f.port.closed, false, 'A gap between players must retain the connection');
  assert.equal(f.sender.track, null, 'Detached players must stop sending while the next player loads');
  assert.equal(old.readyState, 'ended');
  const next = f.addVideo();f.tick(1500);await flush();
  assert.notEqual(f.sender.track, old);assert.equal(old.readyState, 'ended');
  assert.equal(f.messages.filter(m => m.action === 'start').length, 1, 'Switching videos must retain the peer/session');
  const latest = f.sender.track;
  f.source.dispatchEvent(new Event('emptied'));await flush();
  assert.equal(f.sender.track, latest, 'Old player events must no longer affect capture');
  next.ended = true;f.tick(2000);await flush();
  assert.equal(f.port.closed, false);
  f.stop();
}
{
  const f = await fixture();
  const old = f.sender.track;
  const offscreen = f.addVideo();offscreen.rect = {left: 0, top: 1200, right: 960, bottom: 1740};
  f.tick(1000);await flush();assert.equal(f.sender.track, old);
  f.source.rect = {left: 0, top: -600, right: 960, bottom: -60};
  offscreen.rect = {left: 0, top: 0, right: 960, bottom: 540};
  f.tick(1500);await flush();assert.notEqual(f.sender.track, old, 'Follow the visible playing Short');
  f.stop();
}
{
  const f = await fixture('sbs-half', 3840, 2160);
  const original = f.sender.track;
  f.source.isConnected = false;
  const next = f.addVideo();next.videoWidth = 3840;next.videoHeight = 2160;
  f.tick(1000);await flush();f.frame();
  assert.equal(f.sender.track, original, 'Scaled capture must keep its canvas track');
  assert.equal(f.canvas.width, 3840);assert.equal(f.canvas.height, 1080);
  f.stop();assert.equal(f.callbacks.size, 0);
}
