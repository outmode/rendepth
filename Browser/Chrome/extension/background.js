/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
let session = null, nextSession = Date.now();
let imageTransfer = null;
let status = "Choose a playing video.";
let statusExpiresAt = 0;
let limitSource = false;
let statusVersion = 0;
const statusReady = rendepthBrowser.storage.session?.get(['status', 'statusExpiresAt']).then(saved => {
  if (!statusVersion && typeof saved.status === 'string') {
    statusExpiresAt = Number(saved.statusExpiresAt) || 0;
    if (saved.status === 'Image opened in Rendepth.' ||
        (statusExpiresAt && Date.now() >= statusExpiresAt)) {
      setStatus('Choose a playing video.');
    } else status = saved.status;
  }
});
const autoReconnectOrigins = new Set();
const preferencesReady = rendepthBrowser.storage.local.get(["limitSource", "autoReconnectOrigins"]).then(saved => {
  limitSource = saved.limitSource === true;
  for (const origin of Array.isArray(saved.autoReconnectOrigins) ? saved.autoReconnectOrigins : [])
    if (typeof origin === "string") autoReconnectOrigins.add(origin);
});

function setStatus(text, error = false, duration = 0) {
  statusExpiresAt = duration ? Date.now() + duration : 0;
  ++statusVersion;
  status = text;
  void rendepthBrowser.storage.session?.set({status: session
    ? 'Video connection ended. Open a playing video to reconnect.' : text, statusExpiresAt});
  rendepthBrowser.browserAction.setBadgeText({text: error ? "!" : session ? "⏻" : ""});
  rendepthBrowser.browserAction.setBadgeBackgroundColor({color: error ? "#b3261e" : "#fe1c68"});
  rendepthBrowser.browserAction.setBadgeTextColor({color: "#ffffff"});
}

function stop(text = "Stopped.") {
  imageTransfer?.abort();
  imageTransfer = null;
  const previous = session;
  session = null;
  if (previous) {
    clearTimeout(previous.retryTimer);
    rendepthBrowser.tabs.sendMessage(previous.tabId, {action: "cancel", sessionId: previous.id},
      {frameId: previous.frameId}).catch(() => {});
    previous.content?.disconnect();
    previous.native?.disconnect();
  }
  setStatus(text);
}

function suspend(current) {
  clearTimeout(current.retryTimer);
  current.retryVersion = (current.retryVersion || 0) + 1;
  if (session !== current || (current.navigating && !current.content && !current.resuming)) return;
  current.navigating = true;
  current.resuming = false;
  ++current.generation;
  current.qualityNotice = "";
  current.requestId = null;
  // Keep the native port (and its heartbeat) alive across document replacement.
  if (current.nativeStarted) current.native.postMessage({action: "navigate"});
  const content = current.content;
  current.content = null;
  content?.disconnect();
  setStatus("Waiting for the next video. Rendepth stays open.");
}

function retryCapture(current) {
  if (session !== current) return;
  suspend(current);
  const generation = current.generation, retryVersion = current.retryVersion;
  setStatus("Waiting for a capturable video. Retrying every 1 second…");
  current.retryTimer = setTimeout(async () => {
    current.retryTimer = null;
    if (session !== current || generation !== current.generation || retryVersion !== current.retryVersion) return;
    try {
      const tabs = await rendepthBrowser.tabs.query({});
      const tab = tabs.find(tab => tab.id === current.tabId);
      if (session !== current || generation !== current.generation || retryVersion !== current.retryVersion) return;
      // Recovery belongs to this site's capture, never an unrelated destination.
      if (!tab?.url || new URL(tab.url).origin !== current.origin) {
        setStatus("Rendepth is waiting. Use Refresh Video to connect this page.");
        return;
      }
      ++current.generation;
      await inject(current, true);
    } catch (error) {
      if (session === current) setStatus("Waiting for video. Use Refresh Video to reconnect. " + error.message);
    }
  }, 1000);
}

async function inject(current, resume, targetElementId = null) {
  const generation = current.generation;
  current.resuming = true;
  try {
    await rendepthBrowser.tabs.executeScript(current.tabId, {file: "quality.js", frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
    await rendepthBrowser.tabs.executeScript(current.tabId, {file: "capture.js", frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
    const reply = await rendepthBrowser.tabs.sendMessage(current.tabId,
      {action: resume ? "resume" : "start", sessionId: current.id, generation,
        targetElementId, ...current.options}, {frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
    if (reply?.retryable) { retryCapture(current); return; }
    if (!reply?.ok && !reply?.cancelled) throw new Error(reply?.error || "Could not select the video.");
  } catch (error) {
    if (session !== current || generation !== current.generation) return;
    if (resume) {
      setStatus("Waiting for video. Click Refresh Video on this page to reconnect. " + error.message);
    } else {
      stop(error.message);
      setStatus(error.message, true);
    }
  } finally {
    if (session === current && generation === current.generation) current.resuming = false;
  }
}

async function start(tabId, frameId, targetElementId, format, scaleHalf = true, url) {
  await preferencesReady;
  const options = {format, scaleHalf, swap: false, limitSource};
  // A manual reconnect after denied site access can also retain the viewer.
  if (session?.tabId === tabId && session.navigating &&
      JSON.stringify(session.options) === JSON.stringify(options)) {
    clearTimeout(session.retryTimer);
    session.frameId = frameId;
    session.origin = url ? new URL(url).origin : session.origin;
    ++session.generation;
    return inject(session, true, targetElementId);
  }
  stop();
  const current = {id: ++nextSession, tabId, frameId, options,
    origin: url ? new URL(url).origin : null, generation: 0,
    navigating: false, resuming: false, nativeStarted: false};
  session = current;
  setStatus("Connecting to Rendepth…");
  // A native port keeps the MV3 worker alive, including while paused or
  // waiting for a replacement document. The host launches the viewer on SDP.
  connectNative(current);
  await inject(current, false, targetElementId);
}

function connectNative(current) {
  const native = rendepthBrowser.runtime.connectNative("com.outmode.rendepth");
  current.native = native;
  native.onMessage.addListener(message => {
    if (session !== current) return;
    if (message.error) {
      stop(message.error);
      setStatus(message.error, true);
    } else if (!current.navigating && current.content && message.requestId === current.requestId) {
      current.content.postMessage(message);
      if (message.action === "answer") setStatus("Connecting video…");
    }
  });
  native.onDisconnect.addListener(() => {
    if (session !== current) return;
    const error = native.error?.message;
    stop(error || "Rendepth connection closed.");
    if (error) setStatus(`${error} Check the native host installation.`, true);
  });
}

rendepthBrowser.runtime.onConnect.addListener(content => {
  if (content.name !== "rendepth-video" || !content.sender.tab) return;
  let current;
  content.onMessage.addListener(message => {
    if (message.action === "hello") {
      const candidate = session;
      if (!candidate || candidate.id !== message.sessionId ||
          candidate.generation !== message.generation || candidate.tabId !== content.sender.tab.id ||
          candidate.frameId !== (content.sender.frameId ?? 0)) {
        content.disconnect(); return;
      }
      current = candidate;
      const previous = current.content;
      current.content = content;
      if (previous !== content) previous?.disconnect();
      if (!current.native) connectNative(current);
      return;
    }
    if (session !== current || current?.content !== content) return;
    if (message.action === "navigate") {
      suspend(current);
    } else if (message.action === "capture-blocked") {
      retryCapture(current);
    } else if (message.action === "error") {
      stop(message.error);
      setStatus(message.error, true);
    } else if (message.action === "quality") {
      current.qualityNotice = typeof message.text === "string" ? message.text.slice(0, 400) : "";
    } else if (message.action === "waiting") {
      setStatus(message.reason === "cross-origin"
        ? "Waiting for the video source to become available for capture…"
        : "Waiting for the next video on this page…");
    } else if (message.action === "connected") {
      setStatus("Streaming to Rendepth. Audio and playback stay in Chrome.");
    } else if (message.action === "start") {
      current.navigating = false;
      current.nativeStarted = true;
      current.requestId = `${current.id}:${current.generation}`;
      current.native.postMessage({...message, requestId: current.requestId});
    }
  });
  content.onDisconnect.addListener(() => {
    if (current && session === current && current.content === content) suspend(current);
  });
});

rendepthBrowser.tabs.onRemoved.addListener(tabId => {
  if (session?.tabId === tabId) stop("Capture tab closed.");
});
rendepthBrowser.tabs.onUpdated.addListener(async (tabId, change, tab) => {
  const current = session;
  if (!current || current.tabId !== tabId) return;
  if (change.status === "loading") suspend(current);
  if (change.status !== "complete" || !current.navigating || current.resuming) return;
  const generation = current.generation;
  const allowed = autoReconnectOrigins.has(current.origin) && tab.url &&
    new URL(tab.url).origin === current.origin &&
    await rendepthBrowser.permissions.contains({origins: [sitePattern(current.origin)]});
  if (session !== current || generation !== current.generation) return;
  if (!allowed) {
    setStatus("Rendepth is waiting. Use Refresh Video to connect this page.");
    return;
  }
  current.frameId = 0;
  setStatus("Waiting for playback on the new page. Rendepth stays open.");
  void inject(current, true);
});

rendepthBrowser.menus.create({id: "open", title: "Open in Rendepth", contexts: ["video"]});
for (const [id, title] of [["2d", "2D Video"], ["sbs-half", "SBS Half"], ["sbs-full", "SBS Full"]]) {
  rendepthBrowser.menus.create({id, parentId: "open", title, contexts: ["video"]});
}
rendepthBrowser.menus.onClicked.addListener((info, tab) => {
  if (info.parentMenuItemId === "open-image") {
    void startImage(info, tab);
    return;
  }
  if (info.parentMenuItemId !== "open") return;
  void start(tab.id, info.frameId ?? 0, info.targetElementId, info.menuItemId, true, info.pageUrl || tab.url);
});
rendepthBrowser.menus.create({id: "open-image", title: "Open in Rendepth", contexts: ["image"]});
for (const [id, title] of [["image-2d", "2D Photo"], ["image-cross-eye", "Cross-Eye"], ["image-parallel", "Parallel"]])
  rendepthBrowser.menus.create({id, parentId: "open-image", title, contexts: ["image"]});

async function startImage(info, tab) {
  stop();
  const current = new AbortController();
  imageTransfer = current;
  let timeout;
  try {
    const url = new URL(info.srcUrl);
    // Request synchronously from the context-menu gesture.
    const permission = ["http:", "https:"].includes(url.protocol)
      ? rendepthBrowser.permissions.request({origins: [sitePattern(url.origin)]}) : Promise.resolve(true);
    setStatus("Opening image in Rendepth…");
    timeout = setTimeout(() => current.abort(), 120000);
    await openWebImage(info, tab, info.menuItemId === "image-2d" ? "2d" : "sbs-full",
      permission, current.signal, info.menuItemId === "image-cross-eye");
    if (imageTransfer === current) setStatus("Image sent to Rendepth.", false, 5000);
  } catch (error) {
    if (imageTransfer === current) {
      setStatus(error.message || "Could not open the image.", true);
    }
  } finally {
    clearTimeout(timeout);
    if (imageTransfer === current) imageTransfer = null;
  }
}
function sitePattern(origin) {
  const url = new URL(origin);
  return url.protocol + "//" + url.hostname + "/*";
}

// Called synchronously by the popup's checkbox handler to retain its user gesture.
// Both the permission promise and the save belong to this service worker: Chrome
// may unload the popup while showing the permission prompt.
async function setAutoReconnect(siteOrigin, requested) {
  const url = new URL(siteOrigin);
  if (!["http:", "https:"].includes(url.protocol)) return {autoReconnect: false};
  const origin = url.origin;
  const permission = {origins: [sitePattern(origin)]};
  const granted = requested === true ? await rendepthBrowser.permissions.request(permission) : false;
  await preferencesReady;
  if (granted) autoReconnectOrigins.add(origin);
  else autoReconnectOrigins.delete(origin);
  await rendepthBrowser.storage.local.set({autoReconnectOrigins: [...autoReconnectOrigins]});
  if (!granted) {
    await rendepthBrowser.permissions.remove(permission);
    const current = session;
    if (current?.origin === origin && current.navigating) {
      suspend(current);
      rendepthBrowser.tabs.sendMessage(current.tabId, {action: "cancel", sessionId: current.id},
        {frameId: current.frameId}).catch(() => {});
      setStatus("Rendepth is waiting. Use Refresh Video to connect this page.");
    }
  }
  return {autoReconnect: granted};
}

rendepthBrowser.runtime.onMessage.addListener(async (message, sender) => {
  if (message.action === 'quality-limit') return requestPageQuality(message, sender);
  // Do not await before this branch: Chrome carries the popup's click gesture
  // into this synchronous turn so permissions.request can display its prompt.
  if (message.action === 'auto-reconnect' && !sender?.tab)
    return setAutoReconnect(message.origin, message.enabled);
  if (sender?.tab) return;
  await preferencesReady;
  await statusReady;
  if (message.action === "site-status") {
    const url = new URL(message.origin);
    if (!["http:", "https:"].includes(url.protocol)) return {autoReconnect: false};
    const origin = url.origin;
    return {autoReconnect: autoReconnectOrigins.has(origin) &&
      await rendepthBrowser.permissions.contains({origins: [sitePattern(origin)]})};
  }
  if (message.action === "preferences") {
    await rendepthBrowser.storage.local.set({limitSource: message.limitSource === true});
    limitSource = message.limitSource === true;
  }
  if (message.action === "stop") stop();
  if (message.action === "open") {
    const [tab] = await rendepthBrowser.tabs.query({active: true, currentWindow: true});
    await start(tab.id, 0, null, message.format, message.scaleHalf, tab.url);
  }
  if (statusExpiresAt && Date.now() >= statusExpiresAt) setStatus("Choose a playing video.");
  return {status, active: Boolean(session), limitSource, qualityNotice: session?.qualityNotice || "", options: session?.options};
});

async function requestPageQuality(message, sender) {
  const current = session;
  if (!current?.options.limitSource || sender?.tab?.id !== current.tabId ||
      (sender.frameId ?? 0) !== current.frameId ||
      typeof message.token !== 'string' || !/^[a-z0-9-]{1,80}$/.test(message.token)) return {ok: false};
  const results = await chrome.scripting.executeScript({
    target: {tabId: current.tabId, frameIds: [current.frameId]}, world: 'MAIN',
    func: token => {
      if (!['youtube.com', 'youtube-nocookie.com'].some(domain =>
        location.hostname === domain || location.hostname.endsWith('.' + domain))) return false;
      const player = document.querySelector(`[data-rendepth-quality="${token}"]`);
      if (!player || typeof player.getAvailableQualityLevels !== 'function' ||
          typeof player.setPlaybackQualityRange !== 'function') return false;
      const available = player.getAvailableQualityLevels();
      const quality = ['hd1080', 'hd720', 'large', 'medium', 'small', 'tiny'].find(level => available.includes(level));
      if (!quality) return false;
      player.setPlaybackQualityRange(quality, quality);
      return true;
    }, args: [message.token]
  });
  return {ok: results[0]?.result === true};
}
