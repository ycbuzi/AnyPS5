(function () {
  var log = window.xboxLog || function () {};
  var listeners = [];
  var settings = null, settingsRead = false, pageReady = false, stateSent = false;
  var local = Windows.Storage.ApplicationData.current.localFolder;
  var Net = Windows.Networking.Connectivity;

  function reply(o) {
    var e = { data: JSON.stringify(o) };
    for (var i = 0; i < listeners.length; i++) { try { listeners[i](e); } catch (x) { log("host listener " + x.message); } }
  }
  function toast(text) { reply({ toast: text }); }
  function readText(name) {
    return local.tryGetItemAsync(name).then(function (item) { return item ? Windows.Storage.FileIO.readTextAsync(item) : null; });
  }
  function writeText(name, text) {
    return local.createFileAsync(name, Windows.Storage.CreationCollisionOption.replaceExisting).then(function (file) { return Windows.Storage.FileIO.writeTextAsync(file, text); });
  }
  function version() {
    var v = Windows.ApplicationModel.Package.current.id.version;
    return v.major + "." + v.minor + "." + v.build + "." + v.revision;
  }
  function pads() {
    var list = [], gp = navigator.getGamepads ? navigator.getGamepads() : [];
    for (var i = 0; i < gp.length; i++) if (gp[i]) list.push(gp[i].id.replace(/\(.*?\)/g, "").trim());
    return list;
  }
  function drives() {
    var list = [];
    try {
      var props = ["System.FreeSpace", "System.Capacity"];
      return local.getBasicPropertiesAsync().then(function (p) { return p.retrievePropertiesAsync(props); }).then(function (m) {
        var free = m.lookup("System.FreeSpace"), total = m.lookup("System.Capacity");
        if (free != null && total != null) list.push({ name: "Xbox", label: "Internal storage", free: Number(free), total: Number(total) });
        return list;
      }, function () { return list; });
    } catch (e) { return Promise.resolve(list); }
  }
  function sendState() {
    drives().then(function (d) {
      reply({ paused: false, host: true, settings: settings, fullscreenStart: true, pads: pads(), build: version(), program: "AnyPS5 for Xbox", root: "", playing: null, drives: d, platform: "xbox" });
      stateSent = true;
    });
  }
  function maybeStart() { if (pageReady && settingsRead && !stateSent) { sendState(); loadLibrary(); watchLibrary(); } }

  var libStamp = null;
  function watchLibrary() {
    local.tryGetItemAsync("library.json").then(function (item) {
      if (!item) { libStamp = "none"; return null; }
      return item.getBasicPropertiesAsync().then(function (p) {
        var s = p.dateModified.getTime() + ":" + p.size;
        if (libStamp !== null && s !== libStamp) { log("library.json changed"); loadLibrary(); }
        libStamp = s;
      });
    }).then(function () { setTimeout(watchLibrary, 5000); }, function (e) { log("library watch " + e.message); setTimeout(watchLibrary, 5000); });
  }
  function loadLibrary() {
    readText("library.json").then(function (text) {
      var list = [];
      try { list = text ? JSON.parse(text) : []; } catch (e) { log("library.json unreadable " + e.message); }
      window.XBOX_LIBRARY = list;
      log("xbox library " + list.length + " games");
      reply({ library: true });
    }, function (e) { log("library read failed " + e.message); });
  }

  function networkStatus() {
    var profile = Net.NetworkInformation.getInternetConnectionProfile();
    var st = { networking: true, lan: true, connected: false, internet: false, method: null };
    if (!profile) return st;
    var level = profile.getNetworkConnectivityLevel();
    st.connected = level >= 1;
    st.internet = level === Net.NetworkConnectivityLevel.internetAccess;
    st.method = profile.isWlanConnectionProfile ? "Wi-Fi" : "LAN Cable";
    if (profile.isWlanConnectionProfile) {
      var bars = profile.getSignalBars();
      if (bars != null) st.signal = Math.round(bars * 20);
      try { st.ssid = profile.wlanConnectionProfileDetails.getConnectedSsid(); } catch (e) {}
    }
    var adapter = profile.networkAdapter ? profile.networkAdapter.networkAdapterId : null;
    var names = Net.NetworkInformation.getHostNames();
    for (var i = 0; i < names.size; i++) {
      var h = names.getAt(i);
      if (h.type !== Windows.Networking.HostNameType.ipv4 || !h.ipInformation) continue;
      if (adapter && h.ipInformation.networkAdapter.networkAdapterId !== adapter) continue;
      st.ip = h.canonicalName;
      var bits = h.ipInformation.prefixLength, mask = [];
      for (var b = 0; b < 4; b++) { var n = Math.max(0, Math.min(8, bits - b * 8)); mask.push(256 - Math.pow(2, 8 - n)); }
      st.mask = mask.join(".");
      break;
    }
    return st;
  }
  function offset() {
    var m = -new Date().getTimezoneOffset(), sign = m < 0 ? "-" : "+";
    m = Math.abs(m);
    return sign + ("0" + Math.floor(m / 60)).slice(-2) + ":" + ("0" + (m % 60)).slice(-2);
  }
  function datetimeStatus() {
    var zone = "";
    try { zone = new Windows.Globalization.Calendar().getTimeZone(); } catch (e) {}
    return { platform: "xbox", timezone: zone, timezoneLabel: zone, utcOffset: offset(), ntp: true, ntpSynchronized: true, timeSource: "Xbox network time",
      canSetTimezone: false, canSetNtp: false, canSetAutoTimezone: false, autoTimezone: null };
  }
  function languageStatus() {
    var g = Windows.System.UserProfile.GlobalizationPreferences, langs = [];
    for (var i = 0; i < g.languages.size; i++) langs.push(g.languages.getAt(i));
    return { platform: "xbox", region: g.homeGeographicRegion, languages: langs, keyboards: [] };
  }
  var areas = {
    network: { status: networkStatus },
    datetime: { status: datetimeStatus },
    language: { status: languageStatus }
  };
  function sys(m) {
    var area = areas[m.sys], fn = area && area[m.action], r = { id: m.id };
    if (!fn) { r.ok = false; r.unsupported = true; r.error = "Not available on Xbox yet"; }
    else { try { r.data = fn(m.args || {}); r.ok = true; } catch (e) { r.ok = false; r.error = e.message; log("sys " + m.sys + "/" + m.action + " " + e.message); } }
    reply({ sysReply: r });
  }

  function handle(m) {
    if (m.sys) { sys(m); return; }
    if (m.state) { sendState(); return; }
    if (m.settings) { settings = m.settings; writeText("settings.json", JSON.stringify(m.settings)); return; }
    if ("play" in m) {
      if (!m.play) { toast("This game is not converted for Xbox yet"); return; }
      log("play " + m.play);
      Windows.System.Launcher.launchUriAsync(new Windows.Foundation.Uri("anyps5-play:?game=" + encodeURIComponent(m.play))).then(function (ok) {
        log("game part launch " + (ok ? "ok" : "refused"));
        if (!ok) toast("The game could not start");
      }, function (e) { log("game part launch failed " + e.message); toast("The game could not start"); });
      return;
    }
    if (m.rescan) { loadLibrary(); toast("Library reloaded"); return; }
    if (m.exit) { window.close(); return; }
    if (m.power === "off") { log("power off"); setTimeout(function () { try { Windows.ApplicationModel.Core.CoreApplication.exit(); } catch (e) { window.close(); } }, 600); return; }
    if (m.power === "restart") { log("power restart"); location.reload(); return; }
    if (m.rest) return;
    if (m.dnd !== undefined || m.fullscreen || m.fullscreenStart !== undefined || m.resume || m.home || m.wake || m.trophyOwner) return;
    if (m.downloads) return;
    if (m.machines || m.tab || m.pause !== undefined || m.closegame || m.shot) { toast("Not available on Xbox"); return; }
    log("host unknown " + Object.keys(m).join(","));
  }

  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage: function (json) {
      var m;
      try { m = JSON.parse(json); } catch (e) { return; }
      setTimeout(function () { try { handle(m); } catch (e) { log("host " + e.message); } }, 0);
    },
    addEventListener: function (type, fn) { if (type === "message") listeners.push(fn); },
    removeEventListener: function (type, fn) { var i = listeners.indexOf(fn); if (i >= 0) listeners.splice(i, 1); }
  };
  readText("settings.json").then(function (text) {
    try { settings = text ? JSON.parse(text) : null; } catch (e) { settings = null; }
    settingsRead = true; maybeStart();
  }, function () { settingsRead = true; maybeStart(); });
  window.addEventListener("load", function () { pageReady = true; maybeStart(); });
  window.xboxHost = { reply: reply, handle: handle, state: sendState };
})();
