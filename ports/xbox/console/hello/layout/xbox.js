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
  window.addEventListener("load", function () {
    var chain = [];
    try { chain = JSON.parse(sessionStorage.getItem("xboxchain") || "null"); } catch (e) {}
    if (chain === null) chain = window.XBOX_CHAIN || [];
    if (!chain.length) return;
    var next = chain.shift();
    try { sessionStorage.setItem("xboxchain", JSON.stringify(chain)); } catch (e) {}
    setTimeout(function () { log("go " + next); }, 2000);
    setTimeout(function () { location.href = next; }, 3500);
  });
  var seen = {};
  setInterval(function () {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (var i = 0; i < pads.length; i++) {
      if (pads[i] && !seen[pads[i].id]) { seen[pads[i].id] = true; log("pad " + i + " " + pads[i].id + " buttons " + pads[i].buttons.length + " mapping " + pads[i].mapping); }
    }
  }, 2000);
})();
