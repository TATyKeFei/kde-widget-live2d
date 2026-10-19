// Loaded temporarily through KWin's scripting API; no persistent KWin setting changes.
// The host substitutes these JSON string literals, never executable input.
var receiver = @RECEIVER@;
var pluginName = @PLUGIN@;
var timer = new QTimer();
// Sample faster than a 30/60 FPS character. A 33 ms sampler and an independent
// frame timer periodically reuse the same old cursor position, then jump.
timer.interval = 8;
var busy = false;
var pendingTicks = 0;
var lastX, lastY, lastNormal, lastFullscreen;
var idleTicks = 125;

function stop() {
    timer.stop();
    callDBus("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", pluginName);
}

// Reuse one callback rather than allocating a closure for every pointer update.
function acknowledged(keepRunning) {
    busy = false;
    if (keepRunning !== true) stop();
}

timer.timeout.connect(function () {
    if (busy) {
        // The receiver may have exited without unloading us. Do not leave an orphan poller.
        if (++pendingTicks > 375) stop();
        return;
    }
    var window = workspace.activeWindow;
    var pos = workspace.cursorPos;
    var x = pos.x, y = pos.y;
    var normal = !!window && !!window.normalWindow;
    var fullscreen = !!window && !!window.fullScreen;
    // Most idle polls must not allocate snapshot objects or JSON strings.
    if (x === lastX && y === lastY && normal === lastNormal &&
            fullscreen === lastFullscreen && ++idleTicks < 125) return;
    idleTicks = 0;
    lastX = x;
    lastY = y;
    lastNormal = normal;
    lastFullscreen = fullscreen;
    var snapshot = '{"x":' + x + ',"y":' + y + ',"normal":' + normal +
                   ',"fullscreen":' + fullscreen + ',"sampledMs":' + Date.now() + '}';
    busy = true;
    pendingTicks = 0;
    callDBus(receiver, "/org/kde/Live2D/Pointer", "org.kde.Live2D.Pointer", "update", snapshot,
        acknowledged);
});
timer.start();
