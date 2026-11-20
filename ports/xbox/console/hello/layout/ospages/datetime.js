(function () {
  const CAT = "system", SEC = "Date and Time", PER = 9;
  const FORMATS = [["system", "System Default"], ["dmy", "DD/MM/YYYY"], ["mdy", "MM/DD/YYYY"], ["ymd", "YYYY-MM-DD"]];
  let st = null, stErr = "", unsupported = false, zones = null, busy = "";

  function onPage() { return screen === "settings" && setPage && setPage.id === CAT; }
  function redraw() { if (onPage()) drawSettings(); }
  function anchor() { return document.querySelector("#slist .srow.sel"); }

  function zoneOpt() {
    if (!st || st.platform !== "linux" || !st.timezone) return {};
    try { new Intl.DateTimeFormat([], { timeZone: st.timezone }); return { timeZone: st.timezone }; } catch (x) { return {}; }
  }
  function fmtClock(d) { return d.toLocaleTimeString([], Object.assign({ hour: "numeric", minute: "2-digit", hour12: !S.clock24 }, zoneOpt())); }
  function fmtDate(d) {
    const f = S.dateFmt || "system", z = zoneOpt();
    if (f === "system") return d.toLocaleDateString([], Object.assign({ year: "numeric", month: "long", day: "numeric" }, z));
    const p = {}; new Intl.DateTimeFormat("en-US", Object.assign({ year: "numeric", month: "2-digit", day: "2-digit" }, z)).formatToParts(d).forEach(x => { p[x.type] = x.value; });
    if (f === "dmy") return p.day + "/" + p.month + "/" + p.year;
    if (f === "mdy") return p.month + "/" + p.day + "/" + p.year;
    return p.year + "-" + p.month + "-" + p.day;
  }
  window.anyps5FormatDate = fmtDate;

  function refresh() {
    return sysCall("datetime", "status", {}).then(r => {
      if (r.ok) { st = r.data || {}; stErr = ""; unsupported = false; }
      else if (r.unsupported) { unsupported = true; stErr = ""; st = null; }
      else stErr = r.error || "No answer";
      redraw();
    });
  }

  function finish(r, done) {
    busy = "";
    if (r.ok) { if (r.data && r.data.platform) st = r.data; toast(done); }
    else if (r.unsupported) toast("Available in AnyPS5 OS");
    else toast(r.error || "Something went wrong");
    redraw();
  }

  function setNtp(v) {
    if (busy) return;
    busy = "ntp"; redraw();
    sysCall("datetime", "setNtp", { on: !!v }).then(r => finish(r, v ? "Date and time will be set automatically." : "Automatic date and time is off."));
  }

  function setZone(z) {
    if (busy) return;
    busy = "tz"; redraw();
    sysCall("datetime", "setTimezone", { zone: z.id }).then(r => finish(r, "Time zone set to " + z.label + "."));
  }

  function paged(items, page, backFn) {
    const pages = Math.max(1, Math.ceil(items.length / PER)), start = page * PER;
    const m = items.slice(start, start + PER).map(it => [it.label, it.fn, it.html]);
    if (page + 1 < pages) m.push(["Next Page", () => paged(items, page + 1, backFn), `Next Page <span style="opacity:.6">${page + 2} of ${pages}</span>`]);
    if (page > 0) m.push(["Previous Page", () => paged(items, page - 1, backFn), `Previous Page <span style="opacity:.6">${page} of ${pages}</span>`]);
    if (backFn) m.push(["Back", backFn]);
    openMenu(anchor(), m, "psmenu");
  }

  function mark(label, on) { return esc(label) + (on ? " &#10003;" : ""); }

  function openRegions() {
    const cur = st && st.timezone, groups = [], by = {};
    zones.forEach(z => { if (!by[z.group]) { by[z.group] = []; groups.push(z.group); } by[z.group].push(z); });
    paged(groups.map(g => {
      const has = by[g].some(z => z.id === cur);
      return { label: g, html: mark(g, has) + ` <span style="opacity:.6">${by[g].length}</span>`, fn: () => openCities(by[g], openRegions) };
    }), 0, null);
  }

  function openCities(list, backFn) {
    const cur = st && st.timezone;
    paged(list.map(z => ({ label: z.label, html: mark(z.label, z.id === cur), fn: () => setZone(z) })), Math.max(0, Math.floor(list.findIndex(z => z.id === cur) / PER)), backFn);
  }

  function pickZone() {
    if (busy) return;
    if (zones) { openRegions(); return; }
    busy = "zones"; redraw();
    sysCall("datetime", "zones", {}).then(r => {
      busy = "";
      if (r.ok && r.data && r.data.zones && r.data.zones.length) { zones = r.data.zones; redraw(); if (onPage()) openRegions(); return; }
      toast(r.unsupported ? "Available in AnyPS5 OS" : (r.error || "No time zones were found."));
      redraw();
    });
  }

  function pickFormat() {
    const cur = S.dateFmt || "system";
    openMenu(anchor(), FORMATS.map(([k, t]) => [t, () => { S.dateFmt = k; saveSettings(); redraw(); }, mark(t, k === cur)]), "psmenu");
  }

  function zoneText() {
    if (!st || !st.timezone) return "Unknown";
    return esc(st.timezoneLabel || st.timezone) + (st.utcOffset ? " (UTC" + esc(st.utcOffset) + ")" : "");
  }

  function yesNo(v) { return v === true ? "Yes" : v === false ? "No" : "Unknown"; }

  function rows(B) {
    const R = B(), now = new Date();
    if (unsupported) R.info("Set Date and Time Automatically", "Available in AnyPS5 OS");
    else if (stErr) { R.info("Set Date and Time Automatically", esc("Could not check: " + stErr)); R.push({ label: "Check Again", val: "", run: () => { stErr = ""; refresh(); } }); }
    else if (!st) R.info("Set Date and Time Automatically", "Checking...");
    else if (busy === "ntp") R.info("Set Date and Time Automatically", "Setting...");
    else R.tog("Set Date and Time Automatically", () => !!st.ntp, setNtp);
    if (st && !unsupported) {
      R.info("Time Synchronized", yesNo(st.ntpSynchronized));
      if (st.timeSource || st.ntpServer) R.info("Time Server", esc(st.timeSource || st.ntpServer));
      if (st.lastSync) R.info("Last Synchronized", esc(st.lastSync));
    }
    R.info("Current Date and Time", esc(fmtDate(now) + "  " + fmtClock(now)));
    if (st && st.syncNote) R.note(st.syncNote);

    R.sh("Time Zone");
    if (unsupported) {
      let bz = ""; try { bz = Intl.DateTimeFormat().resolvedOptions().timeZone || ""; } catch (x) {}
      R.info("Time Zone", esc(bz || "Unknown"));
      R.info("Change Time Zone", "Available in AnyPS5 OS");
    } else if (!st) R.info("Time Zone", stErr ? "Unknown" : "Checking...");
    else {
      R.push({ label: "Time Zone", val: busy === "zones" ? "Loading..." : busy === "tz" ? "Setting..." : zoneText(), run: pickZone });
      if (st.autoTimezone === true || st.autoTimezone === false) R.info("Set Time Zone Automatically", (st.autoTimezone ? "On" : "Off") + (st.canSetAutoTimezone ? "" : " (Available in AnyPS5 OS)"));
      if (st.supportsDst === false) R.info("Daylight Saving Time", "Not Used");
      else if (st.dst === true || st.dst === false) R.info("Daylight Saving Time", st.dst ? "In Effect" : "Not in Effect");
      if (st.canSetTimezone === false || st.canSetNtp === false) R.note(st.platform === "windows" ? "These follow Windows. Changing the time zone or automatic date and time is available in AnyPS5 OS." : "Changing automatic date and time needs a time sync service on this system.");
    }

    R.sh("Date and Time Format");
    R.tog("24-Hour Clock", () => !!S.clock24, v => { S.clock24 = v; saveSettings(); });
    R.push({ label: "Date Format", val: esc((FORMATS.find(f => f[0] === (S.dateFmt || "system")) || FORMATS[0])[1]), run: pickFormat });
    R.note("Date Format changes the date shown on this page. Other screens show dates in the system format.");
    return R;
  }

  setInterval(() => {
    if (!onPage()) return;
    const c = document.querySelector("#ssec .cur");
    if (c && c.textContent === SEC) drawSettings();
  }, 15000);

  registerOsArea({ id: "datetime", cat: CAT, title: SEC, sections: () => [{ name: SEC, rows }], refresh });
})();
