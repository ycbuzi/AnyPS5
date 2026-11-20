(function () {
  const CAT = "network", AV = "Available in AnyPS5 OS";
  let st = null, stErr = "", stUnsup = false, loading = null;
  let view = "main", wifi = null, wifiErr = "", wifiUnsup = false, scanning = false, busy = "";
  let test = null;

  function redraw() { if (screen === "settings" && setPage && setPage.id === CAT) drawSettings(); }
  function sec(v) { return `<span class="secret">${esc(v)}</span>`; }
  function dim(t) { return `<span style="opacity:.6">${esc(t)}</span>`; }
  function canControl() { return !!(st && st.control); }

  function bars(signal) {
    const lvl = signal == null ? 0 : signal >= 75 ? 4 : signal >= 50 ? 3 : signal >= 25 ? 2 : 1;
    let r = "";
    for (let i = 0; i < 4; i++) r += `<rect x="${i * 5}" y="${12 - i * 4}" width="3.4" height="${4 + i * 4}" rx=".8" fill="currentColor" opacity="${i < lvl ? 1 : .25}"/>`;
    return `<svg viewBox="0 0 19 16" style="width:1.7rem;height:1.4rem;vertical-align:-.15rem">${r}</svg>`;
  }
  const LOCK = `<svg viewBox="0 0 24 24" style="width:1.3rem;height:1.3rem;vertical-align:-.15rem;margin-left:.5rem" fill="none" stroke="currentColor" stroke-width="2"><rect x="5" y="11" width="14" height="9" rx="1.5"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/></svg>`;

  function loadStatus() {
    if (loading) return loading;
    loading = sysCall("network", "status").then(r => {
      loading = null;
      if (r.ok) { st = r.data || {}; stErr = ""; stUnsup = false; }
      else { stUnsup = !!r.unsupported; stErr = r.error || "Could not read the connection status"; }
      redraw();
    });
    return loading;
  }

  function scan(rescan) {
    if (scanning) return;
    scanning = true; wifiErr = ""; redraw();
    sysCall("network", "wifi_list", { rescan: !!rescan }).then(r => {
      scanning = false;
      if (r.ok) { wifi = r.data || { networks: [] }; wifiUnsup = false; }
      else { wifiUnsup = !!r.unsupported; wifiErr = r.error || "Could not search for networks"; }
      redraw();
    });
  }

  function act(action, args, doing, done) {
    if (busy) { toast("Please wait. " + busy); return; }
    busy = doing; redraw();
    return sysCall("network", action, args).then(r => {
      busy = "";
      if (r.ok) { if (done) toast(done(r.data || {})); }
      else toast(r.unsupported ? AV : (r.error || "Something went wrong"));
      return Promise.all([loadStatus(), view === "setup" ? Promise.resolve(scan(false)) : null]).then(() => { redraw(); return r; });
    });
  }

  function connectTo(n, password, hidden) {
    const args = { ssid: n.ssid, hidden: !!hidden };
    if (n.bssid && !hidden) args.bssid = n.bssid;
    if (password) args.password = password;
    act("connect", args, "Connecting to " + n.ssid + "...", () => "Connected to " + n.ssid + ".");
  }

  function askPassword(n, hidden) {
    openOsk("Password for " + n.ssid, "", pw => { if (pw) connectTo(n, pw, hidden); else toast("No password entered."); }, { max: 64, capFirst: false, password: true });
  }

  function pickNetwork(n) {
    if (!canControl()) { toast(AV); return; }
    if (n.inUse) {
      openMenu(null, [["Disconnect", () => act("disconnect", {}, "Disconnecting...", () => "Disconnected from " + n.ssid + ".")],
        ["View Connection Status", () => { view = "status"; srow = 0; redraw(); }]], "psmenu");
      return;
    }
    if (n.secured) askPassword(n, false); else connectTo(n, "", false);
  }

  function setUpManually() {
    if (!canControl()) { toast(AV); return; }
    openOsk("Network Name (SSID)", "", ssid => {
      if (!ssid) return;
      openMenu(null, [["WPA/WPA2/WPA3 Personal", () => askPassword({ ssid }, true)], ["None (Open Network)", () => connectTo({ ssid }, "", true)]], "psmenu");
    }, { max: 32, capFirst: false });
  }

  function testState(k, text, cls) { test[k] = { text, cls }; redraw(); }
  function fail(r) { return r.unsupported ? AV : "Failed"; }

  async function runTest() {
    if (test && test.running) return;
    test = { running: true, ip: { text: "Checking..." }, net: { text: "" }, down: { text: "" }, up: { text: "" } };
    redraw();
    let r = await sysCall("network", "test_ip");
    if (!(r.ok && r.data && r.data.ok)) { testState("ip", fail(r), "bad"); return endTest(false, r.ok ? "No IP address was obtained." : r.error); }
    testState("ip", "Successful", "good");
    testState("net", "Checking...");
    r = await sysCall("network", "test_internet");
    if (!(r.ok && r.data && r.data.ok)) { testState("net", fail(r), "bad"); return endTest(false, r.ok ? (r.data && r.data.error) || "Could not reach the internet." : r.error); }
    testState("net", "Successful", "good");
    testState("down", "Checking...");
    r = await sysCall("network", "test_download", { bytes: 10000000 });
    if (r.ok && r.data) testState("down", r.data.mbps.toFixed(1) + " Mbps", "good"); else testState("down", fail(r), "bad");
    testState("up", "Checking...");
    r = await sysCall("network", "test_upload", { bytes: 2000000 });
    if (r.ok && r.data) testState("up", r.data.mbps.toFixed(1) + " Mbps", "good"); else testState("up", fail(r), "bad");
    endTest(true);
  }
  function endTest(passed, why) {
    test.running = false;
    if (!passed) ["net", "down", "up"].forEach(k => { if (!test[k].text) test[k] = { text: "Not Tested" }; });
    test.why = passed ? "" : (why || "");
    toast(passed ? "Internet connection test finished." : "The internet connection test failed.");
    redraw();
  }
  function testVal(s) { return !s.text ? "" : s.cls === "bad" ? `<span style="color:#ff6b6b">${esc(s.text)}</span>` : esc(s.text); }

  function backRow(R) { R.push({ label: "Back", val: "", run: () => { view = "main"; srow = 0; } }); }

  function mainRows(R) {
    if (!st && !stErr) { R.info("Network", dim("Checking...")); }
    else if (stErr && !st) R.info("Network", esc(stUnsup ? AV : stErr));
    if (canControl()) R.tog("Connect to the Internet", () => !!st.networking, v => act("networking", { on: v }, v ? "Turning on the internet connection..." : "Turning off the internet connection...", () => v ? "Connect to the Internet is on." : "Connect to the Internet is off."));
    else R.push({ label: "Connect to the Internet", val: st ? (st.connected ? "On" : "Off") + " &middot; " + AV : AV, run: () => toast(AV) });
    R.push({ label: "Set Up Internet Connection", val: busy ? dim(busy) : "", run: () => { view = "setup"; srow = 0; scan(true); } });
    R.push({ label: "Test Internet Connection", val: test && test.running ? dim("Testing...") : "", run: () => { view = "test"; srow = 0; runTest(); } });
    if (!canControl() && st) R.note("Windows manages this PC's connection. Turning the connection off, connecting and disconnecting are available in AnyPS5 OS.");
  }

  function setupRows(R) {
    backRow(R);
    if (busy) R.info("Status", dim(busy));
    R.sh("Wired");
    const lan = st && st.lan;
    if (!st) R.info("LAN Cable", dim("Checking..."));
    else if (!lan || !lan.present) R.info("LAN Cable", "No LAN Port");
    else R.info("LAN Cable", lan.connected ? "Connected" + (lan.speedMbps ? " &middot; " + lan.speedMbps + " Mbps" : "") : "Not Connected");
    R.sh("Wi-Fi");
    if (canControl() && st.wifiHw) R.tog("Wi-Fi", () => st.wifiRadio !== false, v => act("radio", { on: v }, v ? "Turning on Wi-Fi..." : "Turning off Wi-Fi...", () => v ? "Wi-Fi is on." : "Wi-Fi is off."));
    R.push({ label: "Search Again", val: scanning ? dim("Searching...") : "", run: () => scan(true) });
    if (scanning && !wifi) R.info("Networks Found", dim("Searching..."));
    else if (wifiErr) R.info("Networks Found", esc(wifiUnsup ? AV : wifiErr));
    else if (wifi && !wifi.present) R.info("Wi-Fi", "No Wi-Fi Device");
    else if (wifi && !wifi.radio) R.info("Wi-Fi", "Off");
    else if (wifi && !wifi.networks.length) R.info("Networks Found", "None");
    else if (wifi) wifi.networks.forEach(n => {
      const val = (n.inUse ? "Connected&nbsp;&nbsp;" : "") + bars(n.signal) + (n.secured ? LOCK : "");
      if (canControl()) R.push({ label: n.ssid, val, run: () => pickNetwork(n) });
      else R.info(n.ssid, val);
    });
    R.push({ label: "Set Up Manually", val: canControl() ? "" : AV, run: setUpManually });
    if (!canControl() && wifi && wifi.present) R.note("This list is read-only on Windows. Connecting to a network is available in AnyPS5 OS.");
  }

  function testRows(R) {
    backRow(R);
    if (!test) { R.push({ label: "Start Test", val: "", run: runTest }); return; }
    R.info("Obtain IP Address", testVal(test.ip));
    R.info("Internet Connection", testVal(test.net));
    R.info("Download Speed", testVal(test.down));
    R.info("Upload Speed", testVal(test.up));
    if (test.why) R.note(test.why);
    if (!test.running) R.push({ label: "Test Again", val: "", run: runTest });
  }

  function statusRows(R, withBack) {
    if (withBack) backRow(R);
    R.push({ label: "Check Again", val: loading ? dim("Checking...") : "", run: () => loadStatus() });
    if (!st) { R.info("Connection Method", stErr ? esc(stUnsup ? AV : stErr) : dim("Checking...")); return; }
    const none = '<span style="opacity:.6">&mdash;</span>';
    R.info("Internet Connection", st.internet ? "Connected" : st.connected ? "Not Connected to the Internet" : "Not Connected");
    R.info("Connection Method", esc(st.method || "Not Connected"));
    if (st.method === "Wi-Fi") {
      R.info("SSID", st.ssid ? sec(st.ssid) : none);
      R.info("Signal Strength", st.signal == null ? none : bars(st.signal) + " " + esc(st.signal + "%"));
      if (st.band) R.info("Frequency Band", esc(st.band));
      if (st.security) R.info("Security Method", esc(st.security));
    }
    R.info("IP Address", st.ip ? sec(st.ip) : none);
    R.info("Subnet Mask", st.mask ? esc(st.mask) : none);
    R.info("Default Gateway", st.gateway ? sec(st.gateway) : none);
    const dns = st.dns || [];
    R.info("Primary DNS", dns[0] ? sec(dns[0]) : none);
    R.info("Secondary DNS", dns[1] ? sec(dns[1]) : none);
    R.info(st.method === "Wi-Fi" ? "MAC Address (Wi-Fi)" : "MAC Address (LAN)", st.mac ? sec(st.mac) : none);
  }

  registerOsArea({
    id: CAT, cat: CAT, title: "Network", catTitle: "Network", newCat: true, after: "access",
    icon: '<path d="M2.5 9a14 14 0 0 1 19 0M5.6 12.3a9.5 9.5 0 0 1 12.8 0M8.7 15.5a5 5 0 0 1 6.6 0"/><circle cx="12" cy="18.8" r="1"/>',
    sections: () => [
      { name: "Settings", rows: B => { const R = B(); if (view === "setup") setupRows(R); else if (view === "test") testRows(R); else if (view === "status") statusRows(R, true); else mainRows(R); return R; } },
      { name: "Connection Status", rows: B => { const R = B(); R.sh("View Connection Status"); statusRows(R, false); return R; } }
    ],
    refresh: () => { view = "main"; return loadStatus(); }
  });
})();
