(function () {
  var lines = [];
  try { lines = JSON.parse(sessionStorage.getItem("xboxlog") || "[]"); } catch (e) {}
  var timer = null;
  function flush() {
    timer = null;
    try {
      var text = lines.join("\r\n");
      var folder = Windows.Storage.ApplicationData.current.localFolder;
      folder.createFileAsync("console-log.txt", Windows.Storage.CreationCollisionOption.replaceExisting).then(function (file) {
        return Windows.Storage.FileIO.writeTextAsync(file, text);
      });
    } catch (e) {}
  }
  function log(text) {
    lines.push(new Date().toISOString() + " " + text);
    if (lines.length > 400) lines.shift();
    try { sessionStorage.setItem("xboxlog", JSON.stringify(lines)); } catch (e) {}
    if (!timer) timer = setTimeout(flush, 500);
  }
  window.xboxLog = log;
  if (!Array.prototype.flatMap) {
    Object.defineProperty(Array.prototype, "flatMap", { configurable: true, writable: true, value: function (fn, self) { return [].concat.apply([], this.map(fn, self)); } });
  }
  if (!Array.prototype.flat) {
    Object.defineProperty(Array.prototype, "flat", { configurable: true, writable: true, value: function () { return [].concat.apply([], this); } });
  }
  log("agent " + navigator.userAgent);
  window.addEventListener("error", function (e) { log("error " + e.message + " @" + (e.filename || "") + ":" + e.lineno); });
  try { navigator.gamepadInputEmulation = "gamepad"; log("gamepad mode " + navigator.gamepadInputEmulation); } catch (e) { log("gamepad mode failed " + e.message); }
  try {
    var view = Windows.UI.ViewManagement.ApplicationView.getForCurrentView();
    view.setDesiredBoundsMode(Windows.UI.ViewManagement.ApplicationViewBoundsMode.useCoreWindow);
    log("bounds core window");
  } catch (e) { log("bounds failed " + e.message); }
  try {
    Windows.UI.Core.SystemNavigationManager.getForCurrentView().addEventListener("backrequested", function (e) { e.handled = true; });
  } catch (e) { log("back failed " + e.message); }
  window.addEventListener("load", function () {
    log("loaded " + location.pathname + " " + window.innerWidth + "x" + window.innerHeight + " games " + ((window.LIBRARY || []).length));
  });
  function state() {
    var name = "?";
    try { name = screen; } catch (e) {}
    try { var g = cur(); if (g && g.title) name += " game [" + g.title + "]"; } catch (e) {}
    var picked = document.querySelector(".sel");
    var text = picked ? (picked.textContent || "").replace(/\s+/g, " ").trim().slice(0, 60) : "";
    var extra = "";
    try { extra = " crow " + crow + " ccard " + ccard + " qsel " + qsel + " cards " + ccCards().length + " cc " + document.getElementById("cc").className + " rest " + restOn() + " help " + helpOn; } catch (e) { extra = " (" + e.message + ")"; }
    return "screen " + name + " sel [" + text + "]" + extra;
  }
  function runKeys(keys, i) {
    if (i >= keys.length) { log("keys done " + state()); return; }
    try { input(keys[i]); } catch (e) { log("input " + keys[i] + " threw " + e.message); }
    setTimeout(function () { log("key " + keys[i] + " -> " + state()); runKeys(keys, i + 1); }, 600);
  }
  function pollKeys() {
    try {
      var folder = Windows.Storage.ApplicationData.current.localFolder;
      folder.tryGetItemAsync("keys.txt").then(function (item) {
        if (!item) { setTimeout(pollKeys, 1500); return; }
        Windows.Storage.FileIO.readTextAsync(item).then(function (text) {
          item.deleteAsync().then(function () {
            var keys = text.split(/\s+/).filter(function (k) { return k; });
            log("keys start " + keys.join(",") + " " + state());
            runKeys(keys, 0);
            setTimeout(pollKeys, keys.length * 600 + 1500);
          });
        });
      }, function () { setTimeout(pollKeys, 1500); });
    } catch (e) { log("keys failed " + e.message); }
  }
  window.addEventListener("load", function () { setTimeout(pollKeys, 1500); });
  var seen = {};
  setInterval(function () {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (var i = 0; i < pads.length; i++) {
      if (pads[i] && !seen[pads[i].id]) { seen[pads[i].id] = true; log("pad " + i + " " + pads[i].id + " buttons " + pads[i].buttons.length + " mapping " + pads[i].mapping); }
    }
  }, 2000);
})();
