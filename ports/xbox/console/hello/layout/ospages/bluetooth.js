(function () {
  const AREA = "bluetooth", CAT = "pad", UNSUP = "Available in AnyPS5 OS";
  const st = { loaded: false, unsupported: false, error: "", available: null, powered: null, canPower: false, canPair: null, adapter: "", devices: [], scanning: false, scanned: false, scanUnsupported: false, found: [], busy: {}, powerBusy: false };
  const TYPE_ICONS = {
    gamepad: () => (typeof ICONS !== "undefined" && ICONS.pad) || '<path d="M7 9h10a4 4 0 0 1 3.9 4.8l-.8 3.6a2 2 0 0 1-3.4 1L14.5 16h-5l-2.2 2.4a2 2 0 0 1-3.4-1l-.8-3.6A4 4 0 0 1 7 9z"/>',
    audio: () => '<path d="M4 14v-2a8 8 0 0 1 16 0v2"/><rect x="3" y="14" width="4" height="6" rx="1.5"/><rect x="17" y="14" width="4" height="6" rx="1.5"/>',
    keyboard: () => '<rect x="2.5" y="6" width="19" height="12" rx="1.5"/><path d="M6 10h.01M9 10h.01M12 10h.01M15 10h.01M18 10h.01M7 14h10"/>',
    mouse: () => '<rect x="6.5" y="3" width="11" height="18" rx="5.5"/><path d="M12 7v3"/>',
    phone: () => '<rect x="7" y="2.5" width="10" height="19" rx="2"/><path d="M11 18h2"/>',
    computer: () => '<rect x="3" y="4.5" width="18" height="12" rx="1.5"/><path d="M8.5 20h7M12 16.5V20"/>',
    other: () => '<path d="M7 7l10 10-5 4V3l5 4L7 17"/>'
  };
  const BT_ICON = '<path d="M7 7l10 10-5 4V3l5 4L7 17"/>';
  function typeIcon(t) { const f = TYPE_ICONS[t] || TYPE_ICONS.other; return `<svg viewBox="0 0 24 24" style="vertical-align:middle;margin-right:.6rem">${f()}</svg>`; }
  function redraw() { if (screen === "settings" && setPage && setPage.id === CAT) drawSettings(); }
  function devName(d) { return d.name && d.name.trim() ? d.name : "Unknown Accessory (" + (d.address || "") + ")"; }
  function stateText(d) {
    if (st.busy[d.address]) return st.busy[d.address];
    if (d.connected === true) return "Connected";
    if (d.connected === false) return "Not Connected";
    return d.state || "Registered";
  }
  function failText(r) { return r.unsupported ? UNSUP : (r.error || "Something went wrong"); }

  function refresh() {
    return sysCall(AREA, "status", {}).then(r => {
      st.loaded = true;
      if (r.ok) {
        const d = r.data || {};
        st.unsupported = false; st.error = "";
        st.available = !!d.available; st.powered = typeof d.powered === "boolean" ? d.powered : null;
        st.canPower = !!d.canPower; st.canPair = !!d.canPair; st.adapter = d.adapter || "";
        st.devices = Array.isArray(d.devices) ? d.devices : [];
        const known = {}; st.devices.forEach(x => { if (x.paired) known[x.address] = true; });
        st.found = st.found.filter(x => !known[x.address]);
      } else { st.unsupported = !!r.unsupported; st.error = r.unsupported ? "" : (r.error || "Could not read Bluetooth"); }
      redraw();
    });
  }

  function setPower(on) {
    if (st.powerBusy) return;
    st.powerBusy = true; redraw();
    sysCall(AREA, "power", { on: !!on }).then(r => {
      st.powerBusy = false;
      if (r.ok) toast(on ? "Bluetooth is on." : "Bluetooth is off."); else toast(failText(r));
      return refresh();
    });
  }

  function runAction(d, action, busyText, doneText) {
    st.busy[d.address] = busyText; redraw();
    return sysCall(AREA, action, { address: d.address }).then(r => {
      delete st.busy[d.address];
      toast(r.ok ? doneText : devName(d) + ": " + failText(r));
      return refresh();
    });
  }

  function deviceMenu(d) {
    if (st.busy[d.address]) return;
    const name = devName(d), anchor = document.querySelector("#slist .srow.sel"), items = [];
    if (d.connected === true) items.push(["Disconnect", () => runAction(d, "disconnect", "Disconnecting...", name + " is disconnected.")]);
    else items.push(["Connect", () => runAction(d, "connect", "Connecting...", name + " is connected.")]);
    items.push(["Remove", () => openMenu(anchor, [["Remove " + name, () => runAction(d, "remove", "Removing...", name + " is removed.")], ["Cancel", () => {}]], "psmenu")]);
    openMenu(anchor, items, "psmenu");
  }

  function scan() {
    if (st.scanning) return;
    if (st.canPair === false || st.scanUnsupported) { toast(UNSUP); return; }
    if (st.powered === false) { toast("Turn on Bluetooth first."); return; }
    st.scanning = true; st.scanned = false; st.found = []; redraw();
    sysCall(AREA, "scan", { seconds: 10 }).then(r => {
      st.scanning = false; st.scanned = true;
      if (r.ok) { st.found = Array.isArray(r.data && r.data.devices) ? r.data.devices : []; toast(st.found.length ? st.found.length + (st.found.length === 1 ? " accessory found." : " accessories found.") : "No accessories found."); }
      else { if (r.unsupported) st.scanUnsupported = true; toast(failText(r)); }
      redraw();
    });
  }

  function pair(d) {
    if (st.busy[d.address]) return;
    const name = devName(d), args = { address: d.address };
    st.busy[d.address] = "Pairing..."; redraw();
    sysCall(AREA, "pair", args).then(r => {
      if (!r.ok) { delete st.busy[d.address]; toast(name + ": " + failText(r)); redraw(); return null; }
      st.busy[d.address] = "Registering..."; redraw();
      return sysCall(AREA, "trust", args).then(t => {
        st.busy[d.address] = "Connecting..."; redraw();
        return sysCall(AREA, "connect", args).then(c => {
          delete st.busy[d.address];
          if (!t.ok) toast(name + " is paired, but AnyPS5 could not remember it: " + failText(t));
          else if (!c.ok) toast(name + " is paired, but could not connect: " + failText(c));
          else toast(name + " is registered and connected.");
          st.found = st.found.filter(x => x.address !== d.address);
          return refresh();
        });
      });
    });
  }

  function accessoriesRows(B) {
    const R = B();
    if (!st.loaded) { R.info("Bluetooth", "Checking..."); return R; }
    if (st.unsupported) { R.info("Bluetooth", UNSUP); return R; }
    if (st.error) { R.info("Bluetooth", esc(st.error)); R.push({ label: "Check Again", val: "", run: () => { refresh(); } }); return R; }
    if (!st.available) R.info("Bluetooth Adapter", "Not Found");
    else {
      if (st.canPower) R.tog("Bluetooth", () => !!st.powered, v => setPower(v));
      else if (st.powered !== null) R.info("Bluetooth", st.powered ? "On" : "Off");
      if (st.powerBusy) R.info("Bluetooth", "Changing...");
      if (st.adapter) R.info("Bluetooth Adapter", esc(st.adapter));
    }
    R.sh("Registered Accessories");
    if (!st.devices.length) R.info("No Accessories Registered", "");
    st.devices.forEach(d => R.push({ label: devName(d), val: typeIcon(d.type) + esc(stateText(d)), run: () => deviceMenu(d) }));
    R.push({ label: "Refresh List", val: "", run: () => { refresh(); } });
    if (!st.canPower) R.note("On this PC, AnyPS5 lists the Bluetooth accessories Windows knows about. Connecting, disconnecting and removing them here is available in AnyPS5 OS; until then use Bluetooth & devices in Windows Settings.");
    return R;
  }

  function addRows(B) {
    const R = B();
    const blocked = st.canPair === false || st.scanUnsupported;
    if (st.scanning) R.info("Searching for Accessories...", "About 10 seconds");
    else R.push({ label: st.scanned ? "Search Again" : "Search for Accessories", val: blocked ? UNSUP : (st.loaded ? "" : "Checking..."), run: scan });
    if (!blocked && st.scanned && !st.scanning) {
      R.sh("Accessories Found");
      if (!st.found.length) R.info("No Accessories Found", "");
      st.found.forEach(d => R.push({ label: devName(d), val: typeIcon(d.type) + esc(st.busy[d.address] || "Pair"), run: () => pair(d) }));
    }
    R.sh("DualSense Wireless Controller");
    R.note("To put a DualSense in pairing mode, hold the Create button and the PS button at the same time until the light bar blinks. Then choose Search for Accessories and select the controller when it appears.");
    return R;
  }

  registerOsArea({
    id: AREA, cat: CAT, title: "Bluetooth Accessories", newCat: false,
    icon: BT_ICON,
    sections: () => [
      { name: "Bluetooth Accessories", rows: B => accessoriesRows(B) },
      { name: "Add Bluetooth Accessory", rows: B => addRows(B) }
    ],
    refresh: refresh
  });
})();
