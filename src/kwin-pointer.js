// Loaded temporarily through KWin's scripting API; no persistent KWin setting changes.
// The host substitutes these JSON string literals, never executable input.
var receiver = @RECEIVER@;
var pluginName = @PLUGIN@;
var timer = new QTimer();
timer.interval = 33;
var busy = false;
var pendingTicks = 0;
var last = "";
var idleTicks = 30;

function stop() {
    timer.stop();
    callDBus("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", pluginName);
}

timer.timeout.connect(function () {
    if (busy) {
        // The receiver may have exited without unloading us. Do not leave an orphan poller.
        if (++pendingTicks > 90) stop();
        return;
    }
    var window = workspace.activeWindow;
    var pos = workspace.cursorPos;
    var snapshot = JSON.stringify({x: pos.x, y: pos.y,
        normal: !!window && !!window.normalWindow,
        fullscreen: !!window && !!window.fullScreen});
    if (snapshot === last && ++idleTicks < 30) return;
    idleTicks = 0;
    last = snapshot;
    busy = true;
    pendingTicks = 0;
    callDBus(receiver, "/org/kde/Live2D/Pointer", "org.kde.Live2D.Pointer", "update", snapshot,
        function (keepRunning) {
            busy = false;
            if (keepRunning !== true) stop();
        });
});
timer.start();
