/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
const MAX_WEB_IMAGE = 32 * 1024 * 1024;

async function webImageBlob(info, tab, signal) {
  const url = new URL(info.srcUrl);
  if (["http:", "https:", "blob:"].includes(url.protocol) && info.targetElementId != null) {
    try {
      // Use the context-menu activeTab grant to snapshot the selected image,
      // without resizing or reading unrelated page content.
      const results = await chrome.scripting.executeScript({
        target: {tabId: tab.id, frameIds: [info.frameId ?? 0]},
        func: url => {
          const image = [...document.images].find(image => (image.currentSrc || image.src) === url);
          if (!image?.complete || !image.naturalWidth) throw new Error('The selected image is no longer available.');
          if (image.naturalWidth > 16384 || image.naturalHeight > 16384 ||
              image.naturalWidth * image.naturalHeight > 32 * 1024 * 1024)
            throw new Error('Image dimensions are too large.');
          const canvas = document.createElement('canvas');
          canvas.width = image.naturalWidth; canvas.height = image.naturalHeight;
          canvas.getContext('2d').drawImage(image, 0, 0);
          const data = canvas.toDataURL('image/png');
          if (data.length > Math.ceil(32 * 1024 * 1024 / 3) * 4 + 64)
            throw new Error('Image exceeds the 32 MiB limit.');
          return data;
        }, args: [info.srcUrl]
      });
      const data = results[0]?.result;
      if (typeof data !== 'string') throw new Error('Could not read the selected image.');
      return (await fetch(data, {signal})).blob();
    } catch (error) {
      signal.throwIfAborted();
      if (url.protocol === "blob:") throw error;
      // Cross-origin images can taint the canvas. Try a regular fetch next;
      // existing host access or the server's CORS policy may permit it.
    }
  }
  if (!["http:", "https:", "data:"].includes(url.protocol))
    throw new Error("This image URL is not supported. Save the image and open it in Rendepth.");
  if (url.protocol === "data:" && info.srcUrl.length > Math.ceil(MAX_WEB_IMAGE / 3) * 4 + 1024)
    throw new Error("Image exceeds the 32 MiB limit.");
  const response = await fetch(info.srcUrl, {credentials: "include", signal});
  if (!response.ok) throw new Error(`Could not fetch the image (HTTP ${response.status}).`);
  const reader = response.body.getReader();
  const chunks = [];
  let size = 0;
  try {
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      size += value.length;
      if (size > MAX_WEB_IMAGE) throw new Error("Image exceeds the 32 MiB limit.");
      chunks.push(value);
    }
  } finally {
    await reader.cancel();
  }
  return new Blob(chunks, {type: response.headers.get("content-type") || "application/octet-stream"});
}

async function openWebImage(info, tab, format, permission, signal, swap = false) {
  if (!await permission) throw new Error("Image access was declined.");
  signal.throwIfAborted();
  const native = rendepthBrowser.runtime.connectNative("com.outmode.rendepth");
  let connectionError;
  let pending;
  const fail = error => {pending?.reject(error); pending = null;};
  const abort = () => {fail(new Error("Image transfer cancelled.")); native.disconnect();};
  signal.addEventListener("abort", abort, {once: true});
  native.onDisconnect.addListener(() => {
    connectionError = new Error(native.error?.message || "Rendepth connection closed.");
    fail(connectionError);
  });
  native.onMessage.addListener(message => {
    if (!pending) return;
    const reply = pending; pending = null;
    if (message.error) reply.reject(new Error(message.error));
    else reply.resolve(message.action);
  });
  const send = async (message, expected) => {
    signal.throwIfAborted();
    if (connectionError) throw connectionError;
    let timeout;
    try {
      const action = await new Promise((resolve, reject) => {
        pending = {resolve, reject};
        timeout = setTimeout(() => fail(new Error("Rendepth image transfer timed out.")), 45000);
        native.postMessage(message);
      });
      if (action !== expected) throw new Error("Unexpected image response. Update the native host.");
    } finally {clearTimeout(timeout);}
  };
  try {
    const bitmap = await createImageBitmap(await webImageBlob(info, tab, signal));
    let png;
    try {
      if (!bitmap.width || !bitmap.height || bitmap.width > 16384 || bitmap.height > 16384 ||
          bitmap.width * bitmap.height > MAX_WEB_IMAGE)
        throw new Error("Image dimensions are too large (maximum 32 megapixels).");
      if ((format === "sbs-full" || format === "sbs-half") && (bitmap.width < 2 || bitmap.width % 2))
        throw new Error("SBS images need an even image width for two equal eye views.");
      const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
      canvas.getContext("2d").drawImage(bitmap, 0, 0);
      png = await canvas.convertToBlob({type: "image/png"});
    } finally {
      bitmap.close();
    }
    if (!png || png.size > MAX_WEB_IMAGE) throw new Error("Image exceeds the 32 MiB PNG limit.");
    const bytes = new Uint8Array(await png.arrayBuffer());
    signal.throwIfAborted();
    await send({action: "image-begin", format, swap, size: bytes.length}, "image-ready");
    for (let offset = 0; offset < bytes.length; offset += 48 * 1024) {
      const data = btoa(String.fromCharCode(...bytes.subarray(offset, offset + 48 * 1024)));
      await send({action: "image-chunk", data}, "image-ready");
    }
    await send({action: "image-end"}, "image-opened");
  } finally {
    signal.removeEventListener("abort", abort);
    native.disconnect();
  }
}
