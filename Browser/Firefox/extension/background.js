/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
let session = null, nextSession = 0;
let status = "Choose a playing video.";
let limitSource = false;
const autoReconnectOrigins = new Set();
const preferencesReady = browser.storage.local.get(["limitSource", "autoReconnectOrigins"]).then(saved => {
  limitSource = saved.limitSource === true;
  for (const origin of Array.isArray(saved.autoReconnectOrigins) ? saved.autoReconnectOrigins : [])
    if (typeof origin === "string") autoReconnectOrigins.add(origin);
});

function setStatus(text, error = false) {
  status = text;
  browser.browserAction.setBadgeText({text: error ? "!" : session ? "⏻" : ""});
  browser.browserAction.setBadgeBackgroundColor({color: error ? "#b3261e" : "#fe1c68"});
  browser.browserAction.setBadgeTextColor({color: "#ffffff"});
}

function stop(text = "Stopped.") {
  const previous = session;
  session = null;
  if (previous) {
    browser.tabs.sendMessage(previous.tabId, {action: "cancel", sessionId: previous.id},
      {frameId: previous.frameId}).catch(() => {});
    previous.content?.disconnect();
    previous.native?.disconnect();
  }
  setStatus(text);
}

function suspend(current) {
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

async function inject(current, resume, targetElementId = null) {
  const generation = current.generation;
  current.resuming = true;
  try {
    await browser.tabs.executeScript(current.tabId, {file: "quality.js", frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
    await browser.tabs.executeScript(current.tabId, {file: "capture.js", frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
    const reply = await browser.tabs.sendMessage(current.tabId,
      {action: resume ? "resume" : "start", sessionId: current.id, generation,
        targetElementId, ...current.options}, {frameId: current.frameId});
    if (session !== current || generation !== current.generation) return;
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
  await inject(current, false, targetElementId);
}

function connectNative(current) {
  const native = browser.runtime.connectNative("com.outmode.rendepth");
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

browser.runtime.onConnect.addListener(content => {
  if (content.name !== "rendepth-video" || !content.sender.tab) return;
  let current;
  content.onMessage.addListener(message => {
    if (message.action === "hello") {
      const candidate = session;
      if (!candidate || candidate.id !== message.sessionId ||
          candidate.generation !== message.generation || candidate.tabId !== content.sender.tab.id) {
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
    } else if (message.action === "error") {
      stop(message.error);
      setStatus(message.error, true);
    } else if (message.action === "quality") {
      current.qualityNotice = typeof message.text === "string" ? message.text.slice(0, 400) : "";
    } else if (message.action === "waiting") {
      setStatus("Waiting for the next video on this page…");
    } else if (message.action === "connected") {
      setStatus("Streaming to Rendepth. Audio and playback stay in Firefox.");
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

browser.tabs.onRemoved.addListener(tabId => {
  if (session?.tabId === tabId) stop("Capture tab closed.");
});
browser.tabs.onUpdated.addListener(async (tabId, change, tab) => {
  const current = session;
  if (!current || current.tabId !== tabId) return;
  if (change.status === "loading") suspend(current);
  if (change.status !== "complete" || !current.navigating || current.resuming) return;
  const generation = current.generation;
  const allowed = autoReconnectOrigins.has(current.origin) && tab.url &&
    new URL(tab.url).origin === current.origin &&
    await browser.permissions.contains({origins: [sitePattern(current.origin)]});
  if (session !== current || generation !== current.generation) return;
  if (!allowed) {
    setStatus("Rendepth is waiting. Use Refresh Video to connect this page.");
    return;
  }
  current.frameId = 0;
  setStatus("Waiting for playback on the new page. Rendepth stays open.");
  void inject(current, true);
});

browser.menus.create({id: "open", title: "Open in Rendepth", contexts: ["video"]});
for (const [id, title] of [["2d", "2D Video"], ["sbs-half", "SBS Half"], ["sbs-full", "SBS Full"]]) {
  browser.menus.create({id, parentId: "open", title, contexts: ["video"]});
}
browser.menus.onClicked.addListener((info, tab) => {
  if (info.parentMenuItemId !== "open") return;
  void start(tab.id, info.frameId ?? 0, info.targetElementId, info.menuItemId, true, info.pageUrl || tab.url);
});
function sitePattern(origin) {
  const url = new URL(origin);
  return url.protocol + "//" + url.hostname + "/*";
}

// Called synchronously by the popup's checkbox handler to retain its user gesture.
// Both the permission promise and the save belong to this persistent page: Firefox
// may unload the popup while showing the permission prompt.
async function setAutoReconnect(siteOrigin, requested) {
  const url = new URL(siteOrigin);
  if (!["http:", "https:"].includes(url.protocol)) return {autoReconnect: false};
  const origin = url.origin;
  const permission = {origins: [sitePattern(origin)]};
  const granted = requested === true ? await browser.permissions.request(permission) : false;
  await preferencesReady;
  if (granted) autoReconnectOrigins.add(origin);
  else autoReconnectOrigins.delete(origin);
  await browser.storage.local.set({autoReconnectOrigins: [...autoReconnectOrigins]});
  if (!granted) {
    await browser.permissions.remove(permission);
    const current = session;
    if (current?.origin === origin && current.navigating) {
      suspend(current);
      browser.tabs.sendMessage(current.tabId, {action: "cancel", sessionId: current.id},
        {frameId: current.frameId}).catch(() => {});
      setStatus("Rendepth is waiting. Use Refresh Video to connect this page.");
    }
  }
  return {autoReconnect: granted};
}

browser.runtime.onMessage.addListener(async message => {
  await preferencesReady;
  if (message.action === "site-status") {
    const url = new URL(message.origin);
    if (!["http:", "https:"].includes(url.protocol)) return {autoReconnect: false};
    const origin = url.origin;
    return {autoReconnect: autoReconnectOrigins.has(origin) &&
      await browser.permissions.contains({origins: [sitePattern(origin)]})};
  }
  if (message.action === "preferences") {
    await browser.storage.local.set({limitSource: message.limitSource === true});
    limitSource = message.limitSource === true;
  }
  if (message.action === "stop") stop();
  if (message.action === "open") {
    const [tab] = await browser.tabs.query({active: true, currentWindow: true});
    await start(tab.id, 0, null, message.format, message.scaleHalf, tab.url);
  }
  return {status, active: Boolean(session), limitSource, qualityNotice: session?.qualityNotice || "", options: session?.options};
});
