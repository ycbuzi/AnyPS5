(function () {
  const CAT = "family";
  const DUR = [30, 60, 90, 120, 180, 240, 300, 360, 0];
  let usage = null, usageState = "checking", usageErr = "", selUid = null, busy = false;

  function profiles() { return (typeof S !== "undefined" && S && Array.isArray(S.profiles)) ? S.profiles : []; }
  function prof(uid) { return uid ? profiles().find(p => p.uid === uid) || null : null; }
  function isTest(u) { if (!u) return false; if (typeof isTestUser === "function") return isTestUser(u); return /^anyps5$/i.test(u.name || ""); }
  function fam(u) {
    const f = Object.assign({ limited: false, dailyMin: 0, from: "", to: "", allowAll: true, games: [], whenUp: "notify" }, (u && u.family) || {});
    f.dailyMin = Math.max(0, Math.round(+f.dailyMin || 0));
    f.games = Array.isArray(f.games) ? f.games.filter(x => typeof x === "string") : [];
    if (!/^\d\d:\d\d$/.test(f.from || "")) f.from = "";
    if (!/^\d\d:\d\d$/.test(f.to || "")) f.to = "";
    if (f.whenUp !== "close") f.whenUp = "notify";
    f.allowAll = f.allowAll !== false;
    f.limited = !!f.limited;
    return f;
  }
  function restricted(u) { if (!u || isTest(u)) return false; const f = fam(u); return f.limited || !f.allowAll; }
  function managerUid() {
    const set = prof(S.familyManager);
    if (set && !isTest(set) && !restricted(set)) return set.uid;
    const first = profiles().find(p => p.uid && !isTest(p) && !restricted(p));
    return first ? first.uid : "";
  }
  function managerName() { const m = prof(managerUid()); return m ? m.name : ""; }
  function meNow() { return (typeof me !== "undefined" && me) ? (prof(me.uid) || me) : null; }
  function canManage() { const m = meNow(); return !!m && !!m.uid && m.uid === managerUid(); }
  function setFam(u, patch) { u.family = Object.assign(fam(u), patch); saveSettings(); }
  function onPage() { return typeof screen !== "undefined" && screen === "settings" && typeof setPage !== "undefined" && setPage && setPage.id === CAT; }
  function redraw() { if (onPage()) drawSettings(); }
  function pad2(n) { return (n < 10 ? "0" : "") + n; }
  function todayStr(d) { d = d || new Date(); return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate()); }
  function toMin(t) { const m = /^(\d\d):(\d\d)$/.exec(t || ""); return m ? (+m[1]) * 60 + (+m[2]) : null; }
  function fmtMin(m) { if (m < 60) return m + " min"; const h = Math.floor(m / 60), r = m % 60; return h + " h" + (r ? " " + r + " min" : ""); }
  function durText(m) { if (!m) return "No Limit"; const h = Math.floor(m / 60), r = m % 60; return (h ? h + (h === 1 ? " Hour" : " Hours") : "") + (h && r ? " " : "") + (r ? r + " Minutes" : ""); }
  function windowText(f) { if (!f.from && !f.to) return "Any Time"; return (f.from || "00:00") + " to " + (f.to || "24:00"); }

  function usedToday(uid) {
    if (!usage || usage.date !== todayStr() || !usage.today) return null;
    const v = usage.today[uid];
    return typeof v === "number" ? v : 0;
  }
  function windowState(f, now) {
    const n = now.getHours() * 60 + now.getMinutes(), a = toMin(f.from), b = toMin(f.to);
    if (a === null && b === null) return { inside: true, toEnd: null };
    if (a !== null && b !== null) {
      if (a === b) return { inside: true, toEnd: null };
      if (a < b) return { inside: n >= a && n < b, toEnd: n >= a && n < b ? b - n : 0 };
      if (n >= a) return { inside: true, toEnd: 1440 - n + b };
      if (n < b) return { inside: true, toEnd: b - n };
      return { inside: false, toEnd: 0 };
    }
    if (a !== null) return { inside: n >= a, toEnd: n >= a ? 1440 - n : 0 };
    return { inside: n < b, toEnd: n < b ? b - n : 0 };
  }
  function minutesLeft(u, now) {
    if (!u || isTest(u)) return null;
    const f = fam(u);
    if (!f.limited) return null;
    const w = windowState(f, now || new Date());
    if (!w.inside) return 0;
    let left = Infinity;
    if (f.dailyMin > 0) left = Math.max(0, f.dailyMin - (usedToday(u.uid) || 0));
    if (w.toEnd !== null) left = Math.min(left, w.toEnd);
    return left === Infinity ? null : left;
  }
  function leftText(u) {
    if (!fam(u).limited) return "No Limit";
    if (fam(u).dailyMin > 0 && usedToday(u.uid) === null) return usageState === "unsupported" ? "Available in AnyPS5 OS" : usageState === "error" ? "Could not read playtime" : "Checking...";
    const l = minutesLeft(u);
    return l === null ? "No Limit" : l === 0 ? "None" : fmtMin(l);
  }

  function refresh() {
    if (busy || typeof sysCall !== "function") return Promise.resolve();
    busy = true;
    return sysCall("family", "usage", {}).then(r => {
      busy = false;
      if (r && r.ok && r.data && typeof r.data === "object") { usage = { date: String(r.data.date || ""), today: r.data.today || {} }; usageState = "ok"; usageErr = ""; }
      else if (r && r.unsupported) { usageState = "unsupported"; usageErr = r.error || ""; }
      else { usageState = "error"; usageErr = (r && r.error) || "No answer"; }
      redraw();
    }, () => { busy = false; usageState = "error"; });
  }

  window.familyCheck = function (user, game) {
    refresh();
    const u = user ? (prof(user.uid) || user) : null;
    if (!u || isTest(u)) return { ok: true, reason: "", minutesLeft: null };
    const f = fam(u), title = (game && game.title) || "this game", left = minutesLeft(u);
    if (!f.allowAll && !(game && game.id && f.games.indexOf(game.id) >= 0))
      return { ok: false, reason: u.name + " is not allowed to play " + title + ". " + (managerName() || "The family manager") + " can allow it in Settings > Family and Parental Controls > Applications.", minutesLeft: left };
    if (f.limited && left === 0) {
      const w = windowState(f, new Date());
      const reason = !w.inside ? u.name + " can play from " + windowText(f).replace(" to ", " until ") + "." : u.name + " has used today's playtime (" + durText(f.dailyMin) + ").";
      return { ok: false, reason: reason, minutesLeft: 0 };
    }
    return { ok: true, reason: left === null ? "" : fmtMin(left) + " of playtime left today", minutesLeft: left };
  };

  function target() {
    const m = meNow();
    if (!canManage()) return m;
    let u = prof(selUid);
    if (!u || isTest(u)) {
      const mu = managerUid();
      u = profiles().find(p => p.uid && !isTest(p) && p.uid !== mu && restricted(p)) || profiles().find(p => p.uid && !isTest(p) && p.uid !== mu) || m;
      selUid = u ? u.uid : null;
    }
    return u;
  }
  function editable(u) { return !!u && canManage() && u.uid !== managerUid() && !isTest(u); }

  function games() {
    const all = (typeof ALL !== "undefined" && Array.isArray(ALL) && ALL.length) ? ALL : (Array.isArray(window.LIBRARY) ? window.LIBRARY : []);
    const seen = {}, out = [];
    all.forEach(g => { if (g && g.id && !g.media && !seen[g.id]) { seen[g.id] = 1; out.push({ id: g.id, title: g.title || g.id }); } });
    return out.sort((a, b) => a.title.localeCompare(b.title));
  }

  function askTime(u, key) {
    const f = fam(u), label = key === "from" ? "Playtime Start" : "Playtime End";
    openMenu(null, [
      ["Choose Time", () => openOsk(label + " (24-hour time, for example 19:30)", f[key] || "", t => {
        const m = /^\s*(\d{1,2})\s*[:.]?\s*(\d{2})\s*$/.exec(t || "");
        if (!m || +m[1] > 23 || +m[2] > 59) { toast("Enter a time from 00:00 to 23:59"); return; }
        const v = pad2(+m[1]) + ":" + m[2], other = key === "from" ? fam(u).to : fam(u).from;
        if (other && other === v) { toast("Playtime Start and Playtime End must be different"); return; }
        const p = {}; p[key] = v; setFam(u, p); toast(label + " for " + u.name + ": " + v); redraw();
      })],
      ["Not Set", () => { const p = {}; p[key] = ""; setFam(u, p); toast(label + " for " + u.name + ": not set"); }]
    ], "psmenu");
  }

  function membersRows(B) {
    const R = B(), m = meNow(), mu = managerUid(), mgr = prof(mu);
    if (!m) { R.note("Log in to see Family and Parental Controls."); return R; }
    if (canManage()) {
      const t = target();
      R.sh("Family Members");
      profiles().filter(p => p.uid && !isTest(p)).forEach(p => {
        const st = p.uid === mu ? "Family Manager" : restricted(p) ? "Restricted" : "No Restrictions";
        R.push({ label: p.name, val: (t && t.uid === p.uid ? "&#10003; " : "") + esc(st), run: () => { selUid = p.uid; toast("Playtime Settings and Applications now show " + p.name); } });
      });
      R.note("Pick a user, then set their Playtime Settings and Applications. The test pipeline user is never restricted.");
      R.sh("Family Manager");
      R.info("Family Manager", esc(mgr ? mgr.name : ""));
      const others = profiles().filter(p => p.uid && p.uid !== mu && !isTest(p) && !restricted(p));
      R.push({ label: "Change Family Manager", val: others.length ? "" : "No Other User", run: () => {
        if (!others.length) { toast("Only a user without restrictions can be the family manager"); return; }
        openMenu(null, others.map(p => [p.name, () => { S.familyManager = p.uid; saveSettings(); toast(p.name + " is now the family manager"); }]), "psmenu");
      } });
      if (mgr && !mgr.pinHash) R.note("Set a passcode for " + mgr.name + " in Users and Accounts > Users, so other users cannot log in as the family manager and change these settings.");
      return R;
    }
    R.info("Family Manager", esc(mgr ? mgr.name : "None"));
    R.note(mgr ? "Only " + mgr.name + " can change Family and Parental Controls." : "No user without restrictions is set up. Add a new user in Users and Accounts to manage these settings.");
    R.sh("Your Restrictions");
    R.info("Status", restricted(m) ? "Restricted" : "No Restrictions");
    if (restricted(m)) { R.info("Playtime Left Today", esc(leftText(m))); R.info("Allowed Games", fam(m).allowAll ? "All Games" : fam(m).games.length + (fam(m).games.length === 1 ? " Game" : " Games")); }
    return R;
  }

  function playtimeRows(B) {
    const R = B(), u = target();
    if (!u) { R.note("Log in to see playtime settings."); return R; }
    const f = fam(u), ed = editable(u);
    R.sh(u.name);
    if (canManage() && u.uid === managerUid()) R.note(u.name + " is the family manager and has no restrictions. Pick another user in Family Members.");
    if (ed) {
      R.tog("Restrict Playtime", () => fam(u).limited, v => setFam(u, { limited: v }));
      R.push({ label: "Duration per Day", val: esc(durText(f.dailyMin)), run: () => openMenu(null, DUR.map(d => [durText(d), () => setFam(u, { dailyMin: d }), (d === f.dailyMin ? "&#10003; " : "") + esc(durText(d))]), "psmenu") });
      R.push({ label: "Playtime Start", val: esc(f.from || "Not Set"), run: () => askTime(u, "from") });
      R.push({ label: "Playtime End", val: esc(f.to || "Not Set"), run: () => askTime(u, "to") });
      R.push({ label: "When Playtime Ends", val: f.whenUp === "close" ? "Close the Game" : "Notify Only", run: () => openMenu(null, [["Notify Only", () => setFam(u, { whenUp: "notify" })], ["Close the Game", () => setFam(u, { whenUp: "close" })]], "psmenu") });
    } else {
      R.info("Restrict Playtime", f.limited ? "On" : "Off");
      R.info("Duration per Day", esc(durText(f.dailyMin)));
      R.info("Playtime", esc(windowText(f)));
      R.info("When Playtime Ends", f.whenUp === "close" ? "Close the Game" : "Notify Only");
    }
    R.sh("Today");
    const used = usedToday(u.uid);
    R.info("Played Today", used !== null ? esc(fmtMin(used)) : usageState === "unsupported" ? "Available in AnyPS5 OS" : usageState === "error" ? "Could not read playtime" : "Checking...");
    R.info("Playtime Left Today", esc(leftText(u)));
    R.note("AnyPS5 counts each user's playtime while a game runs and resets it at midnight. Warnings appear 15, 5 and 1 minute before playtime ends. Playtime Start and End set the hours when games can be played.");
    return R;
  }

  function appRows(B) {
    const R = B(), u = target();
    if (!u) { R.note("Log in to see application settings."); return R; }
    const ed = editable(u), list = games();
    R.sh(u.name);
    if (ed) {
      R.tog("Allow All Games", () => fam(u).allowAll, v => setFam(u, { allowAll: v }));
      if (!fam(u).allowAll) {
        const allowed = fam(u).games;
        R.info("Allowed Games", allowed.length + " of " + list.length);
        R.sh("Games");
        if (!list.length) R.note("No games in the library yet.");
        list.forEach(g => R.push({ label: g.title, val: fam(u).games.indexOf(g.id) >= 0 ? "&#10003; Allowed" : "Not Allowed", run: () => {
          const cur = fam(u).games, i = cur.indexOf(g.id);
          if (i >= 0) cur.splice(i, 1); else cur.push(g.id);
          setFam(u, { games: cur });
        } }));
        const ids = list.map(g => g.id);
        allowed.filter(id => ids.indexOf(id) < 0).forEach(id => R.push({ label: id, val: "&#10003; Allowed (not in the library)", run: () => setFam(u, { games: fam(u).games.filter(x => x !== id) }) }));
        R.note("Only games with a check mark can be started by " + u.name + ".");
      }
      return R;
    }
    const f = fam(u);
    if (canManage() && u.uid === managerUid()) R.note(u.name + " is the family manager and can play every game.");
    R.info("Allow All Games", f.allowAll ? "On" : "Off");
    if (!f.allowAll) {
      R.sh("Allowed Games");
      if (!f.games.length) R.note("No games are allowed.");
      f.games.forEach(id => { const g = list.find(x => x.id === id); R.info(g ? g.title : id, "&#10003;"); });
    }
    return R;
  }

  window.addEventListener("load", () => setTimeout(refresh, 2000));

  registerOsArea({
    id: CAT, cat: CAT, title: "Family and Parental Controls", newCat: true, after: "users", catTitle: "Family and Parental Controls",
    icon: '<circle cx="8" cy="6.5" r="2.8"/><circle cx="16" cy="6.5" r="2.8"/><circle cx="12" cy="13.5" r="2"/><path d="M2.5 19.5c.5-3.5 2.6-6 5.5-6M21.5 19.5c-.5-3.5-2.6-6-5.5-6M8.6 20.5c.5-1.8 1.8-2.9 3.4-2.9s2.9 1.1 3.4 2.9"/>',
    sections: () => [
      { name: "Family Management", rows: membersRows },
      { name: "Playtime Settings", rows: playtimeRows },
      { name: "Applications", rows: appRows }
    ],
    refresh: refresh
  });
})();
