"""Manual protocol/desktop checks for tools/x11_validate_desktop.sh's private Xephyr."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
for name in ("binary", "probe", "model", "session", "evidence"):
    parser.add_argument("--" + name, required=True)
parser.add_argument("--assets", help="Optional real asset tree for shared UI rendering")
args = parser.parse_args()
if (os.environ.get("NEKOMOTE_PRIVATE_X11") != "1"
        or os.environ.get("DISPLAY") == os.environ.get("PARENT_DISPLAY")):
    raise SystemExit("Run through tools/x11_validate_desktop.sh in an isolated root filesystem")

out = Path(args.evidence)
out.mkdir(parents=True, exist_ok=False)
base = [args.binary, "--model", args.model, "--session", args.session,
        "--no-save", "--mute", "--config", str(out / "config.json")]
results = {}
processes = []


def run(name, command, expected_code=0):
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (out / (name + ".log")).write_text(result.stdout + result.stderr)
    assert result.returncode == expected_code, (name, result.returncode, result.stderr)
    return result.stdout


def launch(name, command):
    log = out / (name + ".log")
    stream = log.open("w")
    process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
    processes.append((process, stream))
    return process, log


def start(name, command):
    process, log = launch(name, command)
    for _ in range(150):
        content = log.read_text()
        match = re.search(r"window=(0x[0-9a-f]+)", content)
        assert process.poll() is None, (name, content)
        if match and "X11 local pointer" in content:
            time.sleep(0.8)
            return process, match[1]
        time.sleep(0.1)
    raise RuntimeError("window not initialized: " + name)


def stop(process):
    process.send_signal(signal.SIGTERM)
    assert process.wait(timeout=10) == 0


def capture(name):
    run(name + "-capture", [args.probe, "capture", "root", str(out / (name + ".ppm"))])


def process_sample(process, seconds):
    def ticks():
        fields = Path(f"/proc/{process.pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])

    begin_time, begin_ticks = time.monotonic(), ticks()
    time.sleep(seconds)
    duration, cpu_ticks = time.monotonic() - begin_time, ticks() - begin_ticks
    status = Path(f"/proc/{process.pid}/status").read_text()
    rss_kib = int(re.search(r"VmRSS:\s+(\d+)", status)[1])
    return {"seconds": duration, "rss_kib": rss_kib,
            "cpu_percent_of_one_core": cpu_ticks / os.sysconf("SC_CLK_TCK") / duration * 100}


try:
    run("no-wm", base + ["--frames", "1"], 1)
    results["no_wm_rejected"] = True
    wm, _ = launch("xfwm", ["xfwm4", "--compositor=off"])
    for _ in range(100):
        if "window id" in run("wm-ready", ["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"]):
            break
        time.sleep(0.1)
    else:
        raise RuntimeError("no EWMH window manager")
    run("unknown-output", base + ["--output", "does-not-exist", "--frames", "1"], 1)
    run("list-outputs", [args.binary, "--list-outputs"])
    results["unknown_output_rejected"] = True
    run("diagnostic", base + ["--frames", "12", "--capture", str(out / "diagnostic.ppm")])
    run("capture-exists", base + ["--frames", "1", "--capture", str(out / "diagnostic.ppm")], 1)
    results["diagnostic_capture"] = True

    player, window = start("interactive", base)
    properties = run("properties", ["xprop", "-id", window, "_NET_WM_WINDOW_TYPE",
                                    "_NET_WM_DESKTOP", "_NET_WM_STATE", "WM_HINTS",
                                    "WM_CLIENT_MACHINE"])
    for expected in ("_NET_WM_WINDOW_TYPE_DESKTOP", "4294967295", "_NET_WM_STATE_SKIP_TASKBAR",
                     "_NET_WM_STATE_SKIP_PAGER", "False"):
        assert expected in properties, properties
    run("geometry", ["xwininfo", "-id", window])
    assert "input_rectangles=1" in run("interactive-shape", [args.probe, "shape", window])
    capture("without-xfdesktop")
    results["software_process_mapped"] = process_sample(player, 3)
    run("sample-unmap", [args.probe, "unmap", window])
    time.sleep(0.3)
    results["software_process_unmapped"] = process_sample(player, 3)
    run("sample-map", [args.probe, "map", window])
    for index in range(12):
        run(f"unmap-{index}", [args.probe, "unmap", window])
        time.sleep(0.03)
        run(f"map-{index}", [args.probe, "map", window])
        time.sleep(0.03)
    assert player.poll() is None
    run("resize", ["xrandr", "--output", "default", "--mode", "1024x768"])
    time.sleep(0.8)
    geometry = run("resized-geometry", ["xwininfo", "-id", window])
    assert "Width: 1024" in geometry and "Height: 768" in geometry, geometry
    run("resize-back", ["xrandr", "--output", "default", "--mode", "1280x720"])
    time.sleep(0.5)
    results["properties_shape_map_resize"] = True
    stop(player)

    player, window = start("passthrough", base + ["--input", "passthrough"])
    assert "input_rectangles=0" in run("passthrough-shape", [args.probe, "shape", window])
    stop(player)
    results["passthrough_shape"] = True

    desktop = Path(os.environ["HOME"]) / "Desktop"
    desktop.mkdir(exist_ok=True)
    (desktop / "validation-file.txt").write_text("Private X11 desktop validation fixture\n")
    xfdesktop, _ = launch("xfdesktop", ["xfdesktop"])
    time.sleep(3)
    assert xfdesktop.poll() is None
    capture("xfdesktop-only")
    player, window = start("with-xfdesktop", base)
    capture("with-xfdesktop")
    run("stacking", ["xprop", "-root", "_NET_CLIENT_LIST_STACKING"])
    run("xfdesktop-geometry", ["xwininfo", "-root", "-tree"])
    results["xfdesktop_coexistence_measured"] = True
    stop(player)
    captures = ["without-xfdesktop", "xfdesktop-only", "with-xfdesktop"]
    if args.assets:
        player, window = start("shared-ui", [args.binary, "--assets", args.assets, "--ui-open",
                                             "--no-save", "--mute", "--config",
                                             str(out / "config.json")])
        capture("shared-ui")
        stop(player)
        results["shared_ui_rendered"] = True
        captures.append("shared-ui")
    for name in captures:
        Image.open(out / (name + ".ppm")).save(out / (name + ".png"))
    results["private_config_not_written"] = not (out / "config.json").exists()
    assert results["private_config_not_written"]
    (out / "results.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
finally:
    for process, stream in reversed(processes):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        stream.close()
