/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
let session = null;
let status = "Choose a playing video.";

function setStatus(text, error = false) {
  status = text;
  browser.browserAction.setBadgeText({text: error ? "!" : session ? "ON" : ""});
  browser.browserAction.setBadgeBackgroundColor({color: error ? "#b3261e" : "#a02376"});
}

function stop(text = "Stopped.") {
  const previous = session;
  session = null;
  if (previous) {
    previous.content?.disconnect();
    previous.native?.disconnect();
  }
  setStatus(text);
}

async function start(tabId, frameId, targetElementId, format, swap, scaleHalf = true) {
  stop();
  try {
    await browser.tabs.executeScript(tabId, {file: "capture.js", frameId});
    const reply = await browser.tabs.sendMessage(tabId,
      {action: "start", targetElementId, format, swap, scaleHalf}, {frameId});
    if (!reply?.ok) throw new Error(reply?.error || "Could not select the video.");
  } catch (error) {
    stop(error.message);
    setStatus(error.message, true);
  }
}

browser.runtime.onConnect.addListener(content => {
  if (content.name !== "rendepth-video" || !content.sender.tab) return;
  stop();
  const current = {content, native: browser.runtime.connectNative("com.outmode.rendepth")};
  session = current;
  setStatus("Connecting to Rendepth…");
  content.onMessage.addListener(message => {
    if (session !== current) return;
    if (message.action === "error") {
      stop(message.error);
      setStatus(message.error, true);
    } else if (message.action === "waiting") {
      setStatus("Waiting for the next video on this page…");
    } else if (message.action === "connected") {
      setStatus("Streaming to Rendepth. Audio and playback stay in Firefox.");
    } else {
      current.native.postMessage(message);
    }
  });
  current.native.onMessage.addListener(message => {
    if (session !== current) return;
    if (message.error) {
      stop(message.error);
      setStatus(message.error, true);
    } else {
      content.postMessage(message);
      if (message.action === "answer") setStatus("Connecting video…");
    }
  });
  content.onDisconnect.addListener(() => {
    if (session === current) stop("Video connection closed.");
  });
  current.native.onDisconnect.addListener(() => {
    if (session !== current) return;
    const error = current.native.error?.message;
    stop(error || "Rendepth connection closed.");
    if (error) setStatus(`${error} Check the native host installation.`, true);
  });
});

browser.menus.create({id: "open", title: "Open in Rendepth", contexts: ["video"]});
for (const [id, title] of [["2d", "2D Video"], ["half", "SBS Half"], ["full", "SBS Full"],
  ["half-swap", "SBS Half — Swap Eyes"], ["full-swap", "SBS Full — Swap Eyes"]]) {
  browser.menus.create({id, parentId: "open", title, contexts: ["video"]});
}
browser.menus.onClicked.addListener((info, tab) => {
  if (info.parentMenuItemId === "open") {
    start(tab.id, info.frameId, info.targetElementId,
      info.menuItemId === "2d" ? "2d" : (info.menuItemId.startsWith("half") ? "sbs-half" : "sbs-full"),
      info.menuItemId.endsWith("swap"));
  }
});
browser.runtime.onMessage.addListener(async message => {
  if (message.action === "status") return {status, active: Boolean(session)};
  if (message.action === "stop") stop();
  if (message.action === "open") {
    const [tab] = await browser.tabs.query({active: true, currentWindow: true});
    await start(tab.id, 0, null, message.format, message.swap, message.scaleHalf);
  }
  return {status, active: Boolean(session)};
});
