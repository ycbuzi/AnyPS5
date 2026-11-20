(function () {
  const CAT = "system";
  let st = null, loading = false, busy = "", restart = false;
  let dn = null;
  try { dn = new Intl.DisplayNames(["en"], { type: "language" }); } catch (x) { dn = null; }

  function redraw() { if (screen === "settings" && setPage && setPage.id === CAT) drawSettings(); }

  function langName(tag, fallback) {
    if (tag && dn) { try { const n = dn.of(tag); if (n) return n; } catch (x) { } }
    return fallback || tag || "";
  }

  function localeLabel(l) {
    const enc = (l.id.split(".")[1] || "").split("@")[0];
    let name = l.tag ? langName(l.tag, l.id) : (l.id.indexOf("C") === 0 || l.id.indexOf("POSIX") === 0 ? "Plain (C locale)" : l.id);
    if (enc && !/^utf-?8$/i.test(enc)) name += " (" + enc + ")";
    return name;
  }

  function currentLanguage() {
    const l = st && st.data && st.data.language;
    if (!l) return "";
    if (l.name) return l.name;
    if (l.locale) return localeLabel({ id: l.locale, tag: l.tag });
    return "Not set";
  }

  function refresh() {
    if (loading) return Promise.resolve();
    loading = true;
    return sysCall("language", "status", {}).then(r => { st = r; loading = false; redraw(); });
  }

  function fail(what, r) {
    if (r.unsupported) toast("Available in AnyPS5 OS");
    else toast("Could not " + what + ": " + (r.error || "no answer"));
  }

  function pick(listAction, listKey, setAction, setKey, title, label, isCurrent, doneText) {
    if (busy) return;
    busy = "Loading " + title + "..."; redraw();
    sysCall("language", listAction, {}).then(r => {
      busy = ""; redraw();
      if (!r.ok) { fail("load the list", r); return; }
      const items = (r.data && r.data[listKey] || []).map(it => ({ it, name: label(it) }));
      if (!items.length) { toast("No choices were found"); return; }
      items.sort((a, b) => (isCurrent(b.it) - isCurrent(a.it)) || a.name.localeCompare(b.name));
      openMenu(null, items.map(x => [x.name, () => apply(x), `${esc(x.name)}${isCurrent(x.it) ? " &#10003;" : ""}<span style="float:right;opacity:.6;margin-left:2rem">${esc(x.it.id)}</span>`]), "psmenu");
    });
    function apply(x) {
      if (isCurrent(x.it)) return;
      busy = "Changing " + title + "..."; redraw();
      sysCall("language", setAction, { [setKey]: x.it.id }).then(r => {
        busy = "";
        if (!r.ok) { redraw(); fail("change it", r); return; }
        if (r.data && r.data.restart) restart = true;
        toast(doneText + " " + x.name + (r.data && r.data.restart ? ". Restart AnyPS5 OS to apply." : "."));
        refresh();
      });
    }
  }

  function pickLanguage() {
    const cur = st && st.data && st.data.language ? st.data.language.locale : null;
    pick("locales", "locales", "setLocale", "locale", "Console Language", localeLabel, l => cur !== null && l.id === cur, "Console Language set to");
  }

  function pickLayout() {
    const cur = st && st.data ? (st.data.keyboards || []).map(k => k.id) : [];
    pick("layouts", "layouts", "setLayout", "layout", "Keyboard Layout", l => l.name || l.id, l => cur.length === 1 && cur[0] === l.id, "Keyboard Layout set to");
  }

  function rows(B) {
    const R = B();
    if (!st && !loading) refresh();
    if (busy) R.info(busy, "");
    if (!st) { R.info("Console Language", "Checking..."); R.note("The AnyPS5 screen is in English; more languages come later."); return R; }
    if (!st.ok) {
      R.info("Console Language", st.unsupported ? "Available in AnyPS5 OS" : "Could not read: " + esc(st.error || "no answer"));
      R.note("The AnyPS5 screen is in English; more languages come later.");
      return R;
    }
    const d = st.data || {}, win = d.platform === "windows";
    R.push({ label: "Console Language", val: esc(currentLanguage()), run: pickLanguage });
    const reg = d.region || {};
    if (reg.name || reg.tag) R.info("Region Format", esc(reg.name || langName(reg.tag, reg.locale)));
    if (restart) R.note("Restart AnyPS5 OS to apply.");
    if (win) R.note("This is the Windows display language. Changing it from here is available in AnyPS5 OS.");
    R.note("The AnyPS5 screen is in English; more languages come later.");
    R.sh("Input Languages / Keyboard Layout");
    const kb = d.keyboards || [];
    if (win) {
      if (!kb.length) R.info("Input Languages", "None found");
      kb.forEach(k => R.info(k.langName || k.lang || "Keyboard", esc(k.name || k.id)));
      R.push({ label: "Keyboard Layout", val: esc(kb.map(k => k.name || k.id).join(", ") || "None"), run: pickLayout });
    } else {
      R.push({ label: "Keyboard Layout", val: esc(kb.map(k => k.name || k.id).join(", ") || "Not set"), run: pickLayout });
      if (d.x11Variant) R.info("Layout Variant", esc(d.x11Variant));
      R.info("Text Console Keymap", esc(d.vcKeymap || "Not set"));
    }
    return R;
  }

  registerOsArea({ id: "language", cat: CAT, title: "Language", newCat: false, sections: () => [{ name: "Language", rows }], refresh });
})();
