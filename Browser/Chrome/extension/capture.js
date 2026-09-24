/* Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT */
(() => {
  if (globalThis.rendepthCaptureInstalled) return;
  globalThis.rendepthCaptureInstalled = true;
  let stopCurrent = () => {};
  let currentSessionId, currentRun = 0;
  // Read explicit player state only. Media hosts, URLs and CORS failures do
  // not identify ads. Never change the page's player or playback state.
  const isAdVideo = video => {
    if (video.closest?.('.ad-showing, .vjs-ad-playing, .jw-flag-ads, [data-ad-state="playing"], [data-ad-state="active"], [data-is-ad="true"]')) return true;
    // A separate overlay is evidence only when paired with a content player.
    if (video.classList?.contains("video-overlay")) {
      for (let parent = video.parentElement, depth = 0; parent && depth < 6 &&
           parent !== document.body; parent = parent.parentElement, ++depth) {
        const content = parent.querySelector?.('.video-bg-pic video');
        if (content && content !== video) return true;
      }
    }
    return false;
  };
  const adWait = () => Object.assign(new Error("Waiting for the main video."), {captureBlocked: true});

  rendepthBrowser.runtime.onMessage.addListener(async message => {
    if (message.action === "cancel") {
      if (currentSessionId === message.sessionId) { ++currentRun; stopCurrent(); }
      return {ok: true};
    }
    if (message.action !== "start" && message.action !== "resume") return;
    stopCurrent();
    currentSessionId = message.sessionId;
    const run = ++currentRun;
    if (message.action === "resume") {
      // The next page may have no video yet, or may require a Play click. Hold
      // the native session in the background without starting playback ourselves.
      const ready = await new Promise(resolve => {
        let timer;
        const finish = value => {
          clearInterval(timer);
          window.removeEventListener("pagehide", cancel);
          resolve(value);
        };
        const cancel = () => finish(false);
        stopCurrent = cancel;
        window.addEventListener("pagehide", cancel, {once: true});
        const check = () => {
          const ready = [...document.querySelectorAll("video")].some(video => {
            const rect = video.getBoundingClientRect();
            return !isAdVideo(video) && video.isConnected && !video.paused && !video.ended && video.videoWidth &&
              video.readyState >= 2 && rect.right > 0 && rect.bottom > 0 &&
              rect.left < window.innerWidth && rect.top < window.innerHeight;
          });
          if (ready) finish(true);
        };
        timer = setInterval(check, 1000);
        check();
      });
      if (!ready || run !== currentRun) return {ok: false, cancelled: true};
    }
    let source, stream, peer, port, watchdog, canvas, frameCallback, adObserver;
    const scaleHalf = message.format === "sbs-half" && message.scaleHalf !== false;
    let stopped = false, detached = false;
    let checkQuality = () => {};
    let track, sender, capture, refresh, sourceReset, nextFrame;
    let resetVersion = 0, attachedVersion = 0, refreshPending = false;
    let refreshQueue = Promise.resolve();
    const crossOriginNotice = "Chrome blocked capture of this video’s cross-origin media. Waiting for a capturable video.";
    const stop = () => {
      if (stopped) return;
      stopped = true;
      clearInterval(watchdog);
      adObserver?.disconnect();
      if (frameCallback != null) source?.cancelVideoFrameCallback(frameCallback);
      peer?.close();
      stream?.getTracks().forEach(track => track.stop());
      if (canvas) { canvas.width = 0; canvas.height = 0; }
      port?.disconnect();
      window.removeEventListener("pagehide", navigate);
      source?.removeEventListener("emptied", sourceReset);
      for (const event of ["loadeddata", "playing", "resize"]) source?.removeEventListener(event, refresh);
      source?.removeEventListener("ended", refresh);
    };
    const navigate = () => {
      if (stopped) return;
      try { port?.postMessage({action: "navigate"}); } finally { stop(); }
    };
    stopCurrent = stop;
    const fail = error => {
      if (stopped) return;
      try { port?.postMessage({action: error.name === "SecurityError" || error.captureBlocked
        ? "capture-blocked" : "error", error: error.message}); }
      finally { stop(); }
    };
    // Shorts keeps neighbouring videos in the DOM. Rank the visible playing
    // video, rather than whichever preloaded element happens to be largest.
    const visibleArea = video => {
      const rect = video.getBoundingClientRect();
      return Math.max(0, Math.min(rect.right, window.innerWidth) - Math.max(0, rect.left)) *
        Math.max(0, Math.min(rect.bottom, window.innerHeight) - Math.max(0, rect.top));
    };
    const playingVideo = () => {
      const candidates = [...document.querySelectorAll("video")]
        .filter(video => !isAdVideo(video) && video.isConnected && !video.paused && !video.ended && video.videoWidth && visibleArea(video) > 0)
        .sort((a, b) => visibleArea(b) - visibleArea(a));
      const best = candidates[0];
      // Keep an explicitly selected player while it remains active; do not
      // jump to small hover previews or preloaded offscreen Shorts.
      if (source?.isConnected && !isAdVideo(source) && !source.ended && visibleArea(source) > 0 &&
          (!best || visibleArea(best) < visibleArea(source) * (source.paused ? 0.5 : 1.5))) return source;
      return best;
    };
    const videoForTarget = target => {
      if (target instanceof HTMLVideoElement) return isAdVideo(target) ? playingVideo() : target;
      // Chrome can report a video context while getTargetElement returns a
      // player's overlay (for example Vimeo's vp-target). Prefer its nearest
      // container's visible video over an unrelated larger player on the page.
      for (let container = target; container && container !== document.body &&
           container !== document.documentElement; container = container.parentElement) {
        const videos = [...container.querySelectorAll("video")]
          .filter(video => !isAdVideo(video) && video.isConnected && video.videoWidth && visibleArea(video) > 0)
          .sort((a, b) => visibleArea(b) - visibleArea(a));
        if (videos.length) return videos.find(video => !video.paused && !video.ended) || videos[0];
      }
      // A site may replace the context-menu target before the command arrives.
      return playingVideo();
    };
    const attachListeners = () => {
      source.addEventListener("emptied", sourceReset);
      for (const event of ["loadeddata", "playing", "resize", "ended"]) source.addEventListener(event, refresh);
    };
    const followSource = candidate => {
      if (!candidate || candidate === source) return;
      source.removeEventListener("emptied", sourceReset);
      for (const event of ["loadeddata", "playing", "resize", "ended"]) source.removeEventListener(event, refresh);
      if (frameCallback != null) source.cancelVideoFrameCallback(frameCallback);
      frameCallback = null;
      source = candidate;
      ++resetVersion;
      if (!canvas) {
        capture = source.captureStream || source.mozCaptureStream;
        if (!capture) throw new Error("This Chrome version does not provide video capture.");
      }
      attachListeners();
      if (canvas) frameCallback = source.requestVideoFrameCallback(nextFrame);
    };
    try {
      source = message.targetElementId != null
        ? videoForTarget(rendepthBrowser.menus.getTargetElement(message.targetElementId))
        : playingVideo();
      if (!(source instanceof HTMLVideoElement) && [...document.querySelectorAll("video")].some(video =>
          isAdVideo(video) && !video.paused && visibleArea(video) > 0)) throw adWait();
      if (!(source instanceof HTMLVideoElement)) throw new Error("Select a playing video, then try again.");
      if (source.mediaKeys) throw new Error("This protected video cannot be captured.");
      if (source.paused || !source.videoWidth) throw new Error("Play the video before opening it in Rendepth.");
      if (scaleHalf) {
        // Normalize anamorphic half-SBS to full-SBS before WebRTC encoding.
        // 3840x2160 becomes 3840x1080 without throwing away half the columns.
        // A stable canvas track also survives YouTube replacing its source.
        canvas = document.createElement("canvas");
        const context = canvas.getContext("2d", {alpha: false});
        if (!context || !source.requestVideoFrameCallback)
          throw new Error("This Chrome version cannot scale SBS video.");
        const paint = () => {
          if (isAdVideo(source)) throw adWait();
          if (source.mediaKeys) throw new Error("This protected video cannot be captured.");
          if (!source.isConnected || source.ended || source.readyState < 2 || !source.videoWidth || !source.videoHeight) return;
          const scale = Math.max(1, source.videoWidth * 2 / 3840, source.videoHeight / 1080);
          const width = Math.max(2, Math.floor(source.videoWidth * 2 / scale / 2) * 2);
          const height = Math.max(2, Math.floor(source.videoHeight / scale / 2) * 2);
          if (canvas.width !== width || canvas.height !== height) {
            canvas.width = width; canvas.height = height;
          }
          context.drawImage(source, 0, 0, width, height);
        };
        paint();
        capture = () => canvas.captureStream(60);
        nextFrame = () => {
          frameCallback = null;
          if (stopped) return;
          try {
            paint();
            frameCallback = source.requestVideoFrameCallback(nextFrame);
          } catch (error) { fail(error); }
        };
        frameCallback = source.requestVideoFrameCallback(nextFrame);
      } else {
        capture = source.captureStream || source.mozCaptureStream;
        if (!capture) throw new Error("This Chrome version does not provide video capture.");
      }
      stream = capture.call(source);
      // Playback/audio remain in Chrome. Only captured video enters the peer.
      for (const track of stream.getAudioTracks()) { stream.removeTrack(track); track.stop(); }
      track = stream.getVideoTracks()[0];
      if (!track) throw new Error("Chrome did not expose a video track for this source.");
      track.contentHint = "detail";
      window.addEventListener("pagehide", navigate, {once: true});

      // No external signalling, STUN or TURN service. Native messaging carries
      // only SDP; encrypted video travels directly to the local Rendepth peer.
      peer = new RTCPeerConnection({iceServers: [], bundlePolicy: "max-bundle"});
      const transceiver = peer.addTransceiver(track, {direction: "sendonly", streams: [stream]});
      const codecs = RTCRtpSender.getCapabilities("video").codecs.filter(codec =>
        codec.mimeType.toLowerCase() === "video/vp8");
      if (!codecs.length || !transceiver.setCodecPreferences)
        throw new Error("This Chrome version cannot select the VP8 streaming codec. Update Chrome.");
      transceiver.setCodecPreferences(codecs);
      sender = transceiver.sender;
      const configure = async () => {
        const parameters = sender.getParameters();
        for (const encoding of parameters.encodings) {
          encoding.maxFramerate = 60;
          encoding.maxBitrate = 16000000;
          encoding.scaleResolutionDownBy = canvas ? 1 : Math.max(1, source.videoWidth / 3840, source.videoHeight / 1080);
        }
        parameters.degradationPreference = "maintain-resolution";
        await sender.setParameters(parameters);
      };
      await configure();
      // A YouTube quality switch can empty/reload the same video element and
      // replace its captured track. Retain the peer and the viewer's last frame.
      // Serialize track/parameter updates so stale async work cannot resurrect a
      // stopped session or apply an older size after a newer source reset.
      refresh = () => {
        if (stopped || refreshPending) return;
        refreshPending = true;
        refreshQueue = refreshQueue.then(async () => {
          refreshPending = false;
          if (stopped) return;
          if (isAdVideo(source)) throw adWait();
          followSource(playingVideo());
          if (!source.isConnected || source.ended) {
            if (!canvas && !detached) {
              await sender.replaceTrack(null);
              detached = true;
              stream.getTracks().forEach(t => t.stop());
            }
            return;
          }
          if (!source.videoWidth || source.readyState < 2) return;
          if (source.mediaKeys) throw new Error("This protected video cannot be captured.");
          const version = resetVersion;
          if (detached || (!canvas && attachedVersion !== version) || track.readyState === "ended") {
            const replacement = capture.call(source);
            for (const audio of replacement.getAudioTracks()) { replacement.removeTrack(audio); audio.stop(); }
            const next = replacement.getVideoTracks()[0];
            if (!next) { replacement.getTracks().forEach(t => t.stop()); return; }
            next.contentHint = "detail";
            // Scale for the new source before replacing its track.
            try {
              await configure();
              if (stopped || version !== resetVersion) { replacement.getTracks().forEach(t => t.stop()); return; }
              await sender.replaceTrack(next);
              if (stopped) { replacement.getTracks().forEach(t => t.stop()); return; }
              stream.getTracks().forEach(t => t.stop());
              stream = replacement;
              track = next;
              detached = false;
              attachedVersion = version;
            } catch (error) {
              replacement.getTracks().forEach(t => t.stop());
              throw error;
            }
          } else { await configure(); attachedVersion = version; }
        }).catch(fail);
      };
      sourceReset = () => { ++resetVersion; refresh(); };
      attachListeners();
      if (stopped) return {ok: false, error: "Capture was cancelled."};
      port = rendepthBrowser.runtime.connect({name: "rendepth-video"});
      port.onDisconnect.addListener(stop);
      port.postMessage({action: "hello", sessionId: message.sessionId, generation: message.generation});
      if (typeof MutationObserver !== "undefined") {
        adObserver = new MutationObserver(() => {
          if (!stopped && isAdVideo(source)) fail(adWait());
        });
        adObserver.observe(document.documentElement, {subtree: true, attributes: true,
          attributeFilter: ["class", "data-ad-state", "data-is-ad"]});
      }
      port.onMessage.addListener(async reply => {
        if (stopped) return;
        try {
          if (reply.action === "answer") {
            await peer.setRemoteDescription({type: "answer", sdp: reply.sdp});
            if (!stopped) refresh();
          }
        } catch (error) { fail(error); }
      });
      peer.onconnectionstatechange = () => {
        if (peer.connectionState === "connected") port.postMessage({action: "connected"});
        if (peer.connectionState === "failed") fail(new Error("The local video connection failed. Open the video again."));
      };
      // Gather before sending: setup is bounded and needs no per-frame messages.
      await peer.setLocalDescription(await peer.createOffer());
      const deadline = performance.now() + 15000;
      await new Promise((resolve, reject) => {
        const check = () => {
          if (stopped) reject(new Error("Capture was cancelled."));
          else if (peer.iceGatheringState === "complete") resolve();
          else if (performance.now() >= deadline) reject(new Error("Timed out preparing the local video connection."));
          else { setTimeout(check, 50); }
        };
        check();
      });
      if (stopped) return {ok: false, error: "Capture was cancelled."};
      port.postMessage({action: "start", format: scaleHalf ? "sbs-full" : message.format, swap: Boolean(message.swap),
        transport: "webrtc-vp8", sdp: peer.localDescription.sdp});
      checkQuality = globalThis.rendepthCreateQualityLimiter(message.limitSource === true,
        text => { if (!stopped) port.postMessage({action: "quality", text}); });
      checkQuality(source);
      const connectDeadline = performance.now() + 45000;
      let connected = false, mutedSince = null, waiting = false;
      watchdog = setInterval(() => {
        if (stopped) return;
        if (isAdVideo(source)) { fail(adWait()); return; }
        checkQuality(source);
        const candidate = playingVideo();
        const nextWaiting = !candidate && (!source.isConnected || source.ended || !source.videoWidth);
        if (waiting !== nextWaiting) {
          waiting = nextWaiting;
          port.postMessage({action: waiting ? "waiting" : "connected"});
        }
        if ((candidate && candidate !== source) || !source.isConnected || source.ended ||
            attachedVersion !== resetVersion || track.readyState === "ended") refresh();
        if (!waiting && source.isConnected && !source.ended && track.muted && source.readyState >= 3 && !source.paused && !source.seeking && attachedVersion === resetVersion) {
          mutedSince ??= performance.now();
          if (performance.now() - mutedSince > 5000) {
            fail(Object.assign(new Error("Chrome is not exposing video frames. Waiting for an accessible source."), {captureBlocked: true}));
            return;
          }
        } else { mutedSince = null; }
        connected ||= peer.connectionState === "connected";
        if (!connected && performance.now() > connectDeadline)
          fail(new Error("Timed out connecting to Rendepth. Check the viewer and its WebRTC dependencies."));
      }, 250);
      return {ok: true};
    } catch (error) {
      stop();
      return {ok: false, retryable: error.name === "SecurityError" || error.captureBlocked === true, error: error.name === "SecurityError" ? crossOriginNotice
        : `${error.message} Some sites restrict access to video frames.`};
    }
  });
})();
