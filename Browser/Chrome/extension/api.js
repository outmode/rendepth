/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
// The capture and popup use the same small API surface as the Firefox port.
// Keep Chrome callback/Promise and native-port differences here.
(() => {
  if (globalThis.rendepthChromeAPI) return;
  globalThis.rendepthChromeAPI = true;
  const wrapPort = port => {
    let error;
    return {
      name: port.name, sender: port.sender,
      get error() {return error;},
      postMessage: message => port.postMessage(message),
      disconnect: () => port.disconnect(),
      onMessage: port.onMessage,
      onDisconnect: {addListener(listener) {
        port.onDisconnect.addListener(() => {
          const last = chrome.runtime.lastError;
          error = last ? {message: last.message} : undefined;
          listener();
        });
      }}
    };
  };
  const menus = [];
  const inWorker = typeof document === 'undefined';
  if (inWorker) chrome.runtime.onInstalled.addListener(async () => {
    await chrome.contextMenus.removeAll();
    for (const menu of menus) chrome.contextMenus.create(menu);
  });
  globalThis.rendepthBrowser = {
    storage: chrome.storage, permissions: chrome.permissions,
    browserAction: {
      setBadgeText: options => chrome.action.setBadgeText(options),
      setBadgeBackgroundColor: options => chrome.action.setBadgeBackgroundColor(options),
      // Chrome chooses badge text contrast itself.
      setBadgeTextColor() {}
    },
    runtime: {
      sendMessage: message => chrome.runtime.sendMessage(message),
      connect: options => wrapPort(chrome.runtime.connect(options)),
      connectNative: name => wrapPort(chrome.runtime.connectNative(name)),
      onConnect: {addListener(listener) {chrome.runtime.onConnect.addListener(port => listener(wrapPort(port)));}},
      onMessage: {addListener(listener) {
        chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
          try {
            const result = listener(message, sender);
            if (result === undefined) return false;
            Promise.resolve(result).then(sendResponse, error => sendResponse({error: error.message}));
            return true;
          } catch (error) {sendResponse({error: error.message}); return false;}
        });
      }}
    },
    tabs: {
      query: options => chrome.tabs.query(options),
      sendMessage: (id, message, options) => chrome.tabs.sendMessage(id, message, options),
      onUpdated: chrome.tabs?.onUpdated, onRemoved: chrome.tabs?.onRemoved,
      executeScript: async (tabId, options) => {
        const results = await chrome.scripting.executeScript({
          target: {tabId, frameIds: [options.frameId ?? 0]},
          files: ['api.js', options.file]
        });
        return results.map(result => result.result);
      }
    },
    menus: {
      create: item => menus.push(item),
      onClicked: {addListener(listener) {
        chrome.contextMenus.onClicked.addListener((info, tab) => listener({...info,
          // Chrome exposes the media URL rather than Firefox's DOM target ID.
          targetElementId: info.srcUrl ?? null}, tab));
      }},
      getTargetElement: url => {
        const matches = [...document.querySelectorAll('video,img')].filter(element =>
          (element.currentSrc || element.src) === url);
        const area = element => {
          const r = element.getBoundingClientRect();
          return Math.max(0, Math.min(r.right, innerWidth) - Math.max(0, r.left)) *
            Math.max(0, Math.min(r.bottom, innerHeight) - Math.max(0, r.top));
        };
        return matches.sort((a, b) => area(b) - area(a))[0] ?? null;
      }
    }
  };
})();
