(function () {
  const RES = [["720p", "720p"], ["1080p", "1080p"], ["native", "Native"]];
  const TYPES = [["webm", "WebM"], ["mp4", "MP4"]];
  const LENGTHS = [15, 30, 60, 300, 900];
  let st = null, busy = "", replayTimer = 0, booted = false;

  const pick = (list, v, d) => list.some(x => x[0] === v) ? v : d;
  const nameOf = (list, v) => (list.find(x => x[0] === v) || list[0])[1];
  function lenText(s) {
    s = Math.max(0, Math.round(+s || 0));
    if (s < 60) return s + (s === 1 ? " second" : " seconds");
    if (s % 60 === 0) { const m = s / 60; return m + (m === 1 ? " minute" : " minutes"); }
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
  }
  const lenLabel = s => lenText(s).replace(/^(\d+) (\w)/, (a, n, c) => n + " " + c.toUpperCase());
  function clock(since) {
    const s = Math.max(0, Math.floor(Date.now() / 1000 - since)), h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60;
    return (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s % 60).padStart(2, "0");
  }
  function opt() {
    const v = S.video || {};
    return { resolution: pick(RES, v.res, "720p"), format: pick(TYPES, v.type, "webm"), seconds: LENGTHS.indexOf(v.recent) >= 0 ? v.recent : 60, mic: !!v.mic, keep: v.keep !== false };
  }
  function setOpt(k, val) { S.video = Object.assign({}, S.video || {}, { [k]: val }); saveSettings(); scheduleReplay(); }
  function redraw() { if (screen === "settings" && setPage && setPage.id === "capture") drawSettings(); }
  function failText(r, what) { return r.unsupported ? "Available in AnyPS5 OS" : what + ": " + (r.error || "no answer"); }

  function refresh() {
    return sysCall("recording", "status", {}).then(r => {
      if (r.ok && r.data) st = r.data;
      else st = { supported: false, recording: false, replay: false, reason: r.unsupported ? "Available in AnyPS5 OS" : (r.error || "AnyPS5 did not answer"), failed: !r.unsupported };
      redraw();
      return st;
    });
  }

  function applyReplay() {
    return refresh().then(s => {
      if (!s.supported || !s.replaySupported) return null;
      const o = opt();
      if (!o.keep) return s.replay ? sysCall("recording", "replayStop", {}).then(r => { if (!r.ok) toast(failText(r, "Could not stop recent gameplay")); return refresh(); }) : null;
      return sysCall("recording", "replayStart", { seconds: o.seconds, resolution: o.resolution, format: o.format, mic: o.mic }).then(r => { if (!r.ok) toast(failText(r, "Could not keep recent gameplay")); return refresh(); });
    });
  }
  function scheduleReplay() { clearTimeout(replayTimer); replayTimer = setTimeout(applyReplay, 800); }

  function status() {
    return refresh().then(s => ({ supported: !!s.supported, recording: !!s.recording, replay: !!s.replay, reason: s.reason || "", replaySupported: !!s.replaySupported, replaySeconds: s.replaySeconds || 0, startedAt: s.startedAt || 0 }));
  }
  function start() {
    if (busy) return Promise.resolve({ ok: false, error: "Busy" });
    busy = "start"; redraw();
    const o = opt();
    return sysCall("recording", "start", { resolution: o.resolution, format: o.format, mic: o.mic }).then(r => {
      busy = "";
      if (r.ok) toast(o.mic && r.data && !r.data.micIncluded ? "Recording started (without your microphone)" : "Recording started");
      else toast(failText(r, "Could not start recording"));
      return refresh().then(() => r);
    });
  }
  function stop() {
    if (busy) return Promise.resolve({ ok: false, error: "Busy" });
    busy = "stop"; redraw();
    return sysCall("recording", "stop", {}).then(r => {
      busy = "";
      if (!r.ok) toast(failText(r, "Could not stop recording"));
      else if (!r.data.stopped) toast("No recording was running");
      else toast(r.data.saved ? "Saved video clip" : "The recording stopped but no video was saved");
      return refresh().then(() => r);
    });
  }
  function saveRecent(seconds) {
    if (busy) return Promise.resolve({ ok: false, error: "Busy" });
    const args = {};
    if (seconds !== undefined && seconds !== null) args.seconds = seconds;
    busy = "save"; redraw(); if (st && st.supported) toast("Saving recent gameplay...");
    return sysCall("recording", "saveRecent", args).then(r => {
      busy = "";
      toast(r.ok ? "Saved the last " + lenText(r.data.seconds) + " of gameplay" : failText(r, "Could not save recent gameplay"));
      return refresh().then(() => r);
    });
  }
  function list() { return sysCall("recording", "list", {}).then(r => r.ok && r.data ? r.data.videos || [] : []); }
  function boot() { if (booted) return applyReplay(); booted = true; return applyReplay(); }

  window.REC = { status, start, stop, saveRecent, list, boot, refresh };
  setTimeout(() => { if (!booted) boot(); }, 5000);

  registerOsArea({
    id: "recording", cat: "capture", title: "Video Clips",
    refresh,
    sections: () => [{ name: "Video Clips", rows: B => {
      const R = B(), o = opt();
      if (!st) R.info("Video Recording", "Checking...");
      else if (!st.supported) R.info("Video Recording", st.failed ? esc(st.reason) : "Available in AnyPS5 OS");
      else {
        R.info("Video Recording", busy === "start" ? "Starting..." : busy === "stop" ? "Saving..." : st.recording ? "Recording " + clock(st.startedAt) : "Ready");
        if (st.replaySupported) R.info("Recent Gameplay", busy === "save" ? "Saving..." : st.replay ? "Keeping the last " + lenText(st.replaySeconds) : "Off");
      }
      R.push({ label: "Video Clip Resolution", val: nameOf(RES, o.resolution), run: () => { const i = RES.findIndex(x => x[0] === o.resolution); setOpt("res", RES[(i + 1) % RES.length][0]); } });
      R.push({ label: "Video Clip File Type", val: nameOf(TYPES, o.format), run: () => setOpt("type", o.format === "webm" ? "mp4" : "webm") });
      R.push({ label: "Length of Recent Gameplay Video Clip", val: lenLabel(o.seconds), run: () => setOpt("recent", LENGTHS[(LENGTHS.indexOf(o.seconds) + 1) % LENGTHS.length]) });
      R.tog("Include Your Microphone Audio", () => opt().mic, v => setOpt("mic", v));
      if (st && st.supported && st.replaySupported) R.tog("Keep Recent Gameplay", () => opt().keep, v => setOpt("keep", v));
      if (st && st.supported && st.videoDir) R.info("Videos Folder", `<span class="secret">${esc(st.videoDir)}</span>`);
      if (st && st.supported && st.reason) R.note(st.reason);
      else if (st && !st.supported && !st.failed) R.note((st.reason && st.reason !== "Available in AnyPS5 OS" ? st.reason + " " : "") + "These settings are kept for AnyPS5 OS.");
      if (st && st.supported) R.note("Press the Create button to save recent gameplay or start a new recording. Recent gameplay is kept in memory; a longer length uses more memory. Changing a setting here restarts recent gameplay.");
      return R;
    } }]
  });
})();
