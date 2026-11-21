// Loaded on demand by the Wayland wallpaper. Idle pointers do not generate IPC.
(function () {
    if (!workspace.cursorPosChanged)
        return;

    const timer = new QTimer();
    let pending = false;
    let lastX;
    let lastY;
    timer.singleShot = true;
    timer.interval = 33;
    timer.timeout.connect(publish);
    workspace.cursorPosChanged.connect(schedule);
    publish();

    function schedule() {
        if (!pending && !timer.active)
            timer.start();
    }

    function publish() {
        const point = workspace.cursorPos;
        if (point.x === lastX && point.y === lastY)
            return;
        lastX = point.x;
        lastY = point.y;
        pending = true;
        callDBus('uk.nekopara.NekoMote.Gaze', '/Gaze', 'uk.nekopara.NekoMote.Gaze',
            'Update', point.x, point.y, function () {
                pending = false;
                const current = workspace.cursorPos;
                if (current.x !== lastX || current.y !== lastY)
                    schedule();
            });
    }
})();
