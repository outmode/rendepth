/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
// Site controls are best-effort. Confirm decoded dimensions, never assume that
// calling a site's undocumented quality method changed the source.
globalThis.rendepthCreateQualityLimiter = (enabled, report) => {
  let previousSource, previousURL, attempts = 0, nextAttempt = 0, lastNotice = "";
  const notify = text => {
    if (text !== lastNotice) { lastNotice = text; report(text); }
  };
  const manual = "Select 1080p or lower in the website’s video quality menu to reduce browser stutter. Automatic quality control is unavailable or was ignored.";
  return source => {
    if (!enabled) return;
    const url = window.location.href;
    if (source !== previousSource || url !== previousURL) {
      previousSource = source; previousURL = url;
      attempts = 0; nextAttempt = 0;
      notify("");
    }
    if (!source.isConnected || source.ended || source.readyState < 2 || source.mediaKeys ||
        !source.videoWidth || !source.videoHeight) return;
    if (Math.min(source.videoWidth, source.videoHeight) <= 1080 &&
        Math.max(source.videoWidth, source.videoHeight) <= 1920) {
      notify("Source is within the 1080p limit.");
      attempts = 0; nextAttempt = 0;
      return;
    }
    const now = performance.now();
    if (now < nextAttempt) return;
    if (attempts >= 3) { notify(manual); return; }
    ++attempts;
    nextAttempt = now + 3000;
    try {
      const host = window.location.hostname;
      if (!["youtube.com", "youtube-nocookie.com"].some(domain => host === domain || host.endsWith(`.${domain}`))) {
        attempts = 3; notify(manual); return;
      }
      const player = source.closest(".html5-video-player");
      if (!player) {notify(manual); return;}
      const token = crypto.randomUUID();
      player.setAttribute('data-rendepth-quality', token);
      // Site player methods live in Chrome's MAIN world. The worker executes
      // only this fixed quality operation; no page-supplied code is evaluated.
      const expectedSource = source, expectedURL = url;
      rendepthBrowser.runtime.sendMessage({action: 'quality-limit', token}).then(reply => {
        if (source !== previousSource || expectedURL !== previousURL || expectedSource !== previousSource) return;
        notify(reply?.ok ? "Requesting a source quality of 1080p or lower…" : manual);
      }).catch(() => notify(manual)).finally(() => {
        if (player.getAttribute('data-rendepth-quality') === token) player.removeAttribute('data-rendepth-quality');
      });
    } catch (_) {
      notify(manual);
    }
  };
};
// tabs.executeScript clones the script's completion value back to the caller.
// The assignment above evaluates to a function, which cannot be cloned.
void 0;
