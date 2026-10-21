#!/usr/bin/env python3
"""Opt-in X11 GL/Vulkan checks inside a private virtual KWin/XWayland session.

The harness never connects clients to the user's display, changes desktop
configuration or injects input. GPU checks are serialized by the project lock.
Diagnostic captures test client pixels; compositor output is not read back.
"""
import argparse
import fcntl
import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

from run_layer_shell_test import private_environment, read_ppm, stop, write_fixture
from configuration_checks import check_profile_lifecycle

REPO = pathlib.Path(__file__).resolve().parents[2]


def executable(value):
    path = pathlib.Path(value).resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise argparse.ArgumentTypeError(f"not executable: {path}")
    return path


def material_pixels(pixels):
    return sum(1 for i in range(0, len(pixels), 3)
               if all(abs(pixels[i + j] - c) <= 2
                      for j, c in enumerate((247, 23, 191))))


def run_worker(args):
    root = args.evidence_dir
    env = dict(os.environ)
    private_root = args.private_root
    if (not private_root or env.get("NEKOMOTE_X11_PRIVATE_ROOT") != str(private_root)
            or pathlib.Path(env.get("XDG_RUNTIME_DIR", "")) != private_root / "runtime"
            or pathlib.Path(env.get("HOME", "")) != private_root / "home"
            or not env.get("DBUS_SESSION_BUS_ADDRESS")
            or not re.fullmatch(r":\d+(?:\.\d+)?", env.get("DISPLAY", ""))):
        raise RuntimeError("worker requires this harness's private KWin/XWayland session")
    result = dict(scope="isolated KWin virtual XWayland GL/Vulkan client checks",
                  binary=str(args.binary),
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  compositor=str(args.compositor), cases=[], passed=False,
                  environment={key: env.get(key) for key in (
                      "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                      "LIBGL_ALWAYS_SOFTWARE", "NEKOMOTE_VK_VALIDATION")},
                  not_covered=["native Xorg login", "other WMs/drivers", "physical outputs",
                               "real input/audio", "performance/long-term",
                               "compositor final output pixels", "RandR output resize/hotplug"])

    def validation(name, log):
        if env.get("NEKOMOTE_VK_VALIDATION") == "1":
            if "[validation]" in log or "validation errors=0 warnings=0" not in log:
                raise RuntimeError(f"{name}: validation warning/error or missing 0/0 summary")

    def check(name, command, *, success=True, contains=None, vulkan=False):
        completed = subprocess.run(command, env=env, capture_output=True, text=True,
                                   timeout=args.timeout, stdin=subprocess.DEVNULL)
        log = completed.stdout + completed.stderr
        (root / f"{name}.log").write_text(log, encoding="utf-8")
        if (completed.returncode == 0) != success:
            raise RuntimeError(f"{name}: unexpected exit {completed.returncode}; see log")
        if contains and contains not in log:
            raise RuntimeError(f"{name}: expected {contains!r}; see log")
        if success and vulkan:
            validation(name, log)
        case = dict(name=name, command=[str(value) for value in command],
                    exit_code=completed.returncode, passed=True)
        result["cases"].append(case)
        return completed, case

    def inspect_capture(path, case, *, material=True, dimensions=(640, 480)):
        w, h, pixels = read_ppm(path)
        if (w, h) != dimensions:
            raise RuntimeError(f"{path.name}: incorrect dimensions {w}x{h}")
        colored = material_pixels(pixels)
        if material and colored < 4:
            raise RuntimeError(f"{path.name}: synthetic material absent")
        if not material and len(set(zip(pixels[::3], pixels[1::3], pixels[2::3]))) < 16:
            raise RuntimeError(f"{path.name}: empty/uniform image")
        case.update(width=w, height=h, material_pixels=colored,
                    pixels_sha256=hashlib.sha256(pixels).hexdigest())
        return pixels

    def compare(name, first, second):
        differences = [abs(a - b) for a, b in zip(first, second)]
        maximum = max(differences, default=0)
        mean = sum(differences) / max(len(differences), 1)
        case = dict(name=name, max_channel_difference=maximum,
                    mean_channel_difference=mean,
                    differing_channels=sum(value != 0 for value in differences),
                    passed=len(first) == len(second) and maximum <= 3 and mean <= 0.05)
        result["cases"].append(case)
        if not case["passed"]:
            raise RuntimeError(f"{name}: GL/Vulkan image difference exceeded max 3 / mean .05")

    try:
        model, session = write_fixture(root, args.importer, args.timeout)
        result["inputs_sha256"] = {str(path.relative_to(root)):
            hashlib.sha256(path.read_bytes()).hexdigest() for path in (model, session)}
        listing, _ = check("outputs", [str(args.binary), "--list-outputs"])
        if not listing.stdout.strip():
            raise RuntimeError("private XWayland reported no active output")
        common = [str(args.binary), "--model", str(model), "--session", str(session),
                  "--config", str(root / "wallpaper.json")]
        check("unknown-output", common + ["--output", "NEKOMOTE-MISSING", "--frames", "1"],
              success=False, contains="requested XRandR output is not active")
        images = {}
        for backend in ("opengl", "vulkan"):
            for mode in ("model", "assets-ui"):
                name = f"{backend}-{mode}"
                capture = root / f"{name}.ppm"
                source = common if mode == "model" else [str(args.binary), "--assets",
                    str(root / "assets"), "--config", str(root / "wallpaper.json"), "--ui-open"]
                _, case = check(name, source + ["--backend", backend, "--frames", "4",
                                               "--capture", str(capture)], vulkan=backend == "vulkan")
                images[name] = inspect_capture(capture, case)
            if args.real_assets:
                capture = root / f"{backend}-real-ui.ppm"
                _, case = check(f"{backend}-real-ui", [str(args.binary), "--assets",
                    str(args.real_assets), "--backend", backend, "--ui-open", "--config",
                    str(root / "real-wallpaper.json"), "--frames", "4", "--capture", str(capture)],
                    vulkan=backend == "vulkan")
                images[f"{backend}-real-ui"] = inspect_capture(capture, case, material=False)
        for mode in ("model", "assets-ui"):
            compare(f"compare-{mode}", images[f"opengl-{mode}"], images[f"vulkan-{mode}"])
        if args.real_assets:
            compare("compare-real-ui", images["opengl-real-ui"], images["vulkan-real-ui"])
        for backend in ("opengl", "vulkan"):
            result["cases"].extend(check_profile_lifecycle(
                args.binary, backend, args.real_assets or root / "assets", root, env,
                "X11 wallpaper window=", args.timeout))
        capture = root / "vulkan-repeat.ppm"
        _, case = check("vulkan-repeat", common + ["--backend", "vulkan", "--frames", "4",
            "--capture", str(capture)], vulkan=True)
        if inspect_capture(capture, case) != images["vulkan-model"]:
            raise RuntimeError("identical Vulkan fixed-step runs differed")

        # No --capture: this path exercises swapchain submission/presentation.
        for mode, expected in (("passthrough", "input_rectangles=0"),
                               ("interactive", "input_rectangles=1")):
            name = "present-" + mode
            log_path = root / f"{name}.log"
            with log_path.open("w") as log:
                client = subprocess.Popen(common + ["--backend", "vulkan", "--mute", "--no-save",
                    "--input", mode], env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + args.timeout
                    window = None
                    while window is None:
                        match = re.search(r"X11 wallpaper window=(0x[0-9a-fA-F]+)", log_path.read_text())
                        if match:
                            window = match.group(1)
                            break
                        if client.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError(f"{name}: startup failed; see log")
                        time.sleep(0.05)
                    check(name + "-shape", [str(args.probe), "shape", window], contains=expected)

                    def presented(label, dimensions=(640, 480)):
                        deadline = time.monotonic() + args.timeout
                        while True:
                            path = root / f"{label}.ppm"
                            _, case = check(label, [str(args.probe), "capture", window, str(path)])
                            pixels = read_ppm(path)[2]
                            if material_pixels(pixels) >= 4 and read_ppm(path)[:2] == dimensions:
                                inspect_capture(path, case, dimensions=dimensions)
                                return
                            result["cases"].pop()
                            if client.poll() is not None or time.monotonic() >= deadline:
                                raise RuntimeError(f"{label}: no rendered material in window pixels")
                            time.sleep(0.05)

                    presented(name + "-initial")
                    for index in range(4):
                        check(f"{name}-unmap-{index}", [str(args.probe), "unmap", window])
                        time.sleep(0.1)
                        if client.poll() is not None:
                            raise RuntimeError(f"{name}: client exited when hidden")
                        check(f"{name}-map-{index}", [str(args.probe), "map", window])
                        time.sleep(0.1)
                        presented(f"{name}-restored-{index}")
                    # Resize only this exact test-owned XID; this exercises window
                    # ConfigureNotify/output-size handling, not RandR topology.
                    for index, dimensions in enumerate(((480, 360), (640, 480))):
                        check(f"{name}-resize-{index}", [str(args.probe), "resize", window,
                              str(dimensions[0]), str(dimensions[1])])
                        time.sleep(0.1)
                        presented(f"{name}-resized-{index}", dimensions)
                    client.terminate()
                    code = client.wait(timeout=args.timeout)
                    if code:
                        raise RuntimeError(f"{name}: SIGTERM exit {code}")
                    validation(name, log_path.read_text())
                    result["cases"].append(dict(name=name + "-signal-cleanup", exit_code=code,
                                               window_id=window, passed=True))
                finally:
                    stop(client)
        if args.abi_host:
            from PIL import Image
            abi_images = {}
            for backend in ("opengl", "vulkan"):
                capture = root / f"abi-{backend}.png"
                _, case = check(f"abi-{backend}", [str(args.abi_host), "--model", str(model),
                    "--session", str(session), "--backend", backend, "--width", "640",
                    "--height", "480", "--steps", "4", "--capture", str(capture)],
                    vulkan=backend == "vulkan")
                with Image.open(capture) as image:
                    rgba = image.convert("RGBA")
                    black = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
                    black.alpha_composite(rgba)
                    abi_images[backend] = black.convert("RGB").tobytes()
                    if rgba.size != (640, 480) or material_pixels(abi_images[backend]) < 4:
                        raise RuntimeError(f"ABI {backend}: synthetic material or dimensions invalid")
                case["pixels_sha256"] = hashlib.sha256(abi_images[backend]).hexdigest()
            compare("compare-abi-host", abi_images["opengl"], abi_images["vulkan"])
        if any((root / name).exists() for name in ("wallpaper.json", "real-wallpaper.json", "wallpaper.panel.json", "real-wallpaper.panel.json")):
            raise RuntimeError("diagnostic/no-save execution persisted configuration")
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        (root / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=executable)
    parser.add_argument("--probe", required=True, type=executable)
    parser.add_argument("--abi-host", type=executable, help="optional migrated C host regression")
    parser.add_argument("--importer", required=True, type=executable)
    parser.add_argument("--compositor", required=True, type=executable)
    parser.add_argument("--real-assets", type=lambda value: pathlib.Path(value).resolve(strict=True))
    parser.add_argument("--evidence-dir", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--private-worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--private-root", type=pathlib.Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 120:
        parser.error("--timeout must be between 1 and 120 seconds")
    args.evidence_dir = args.evidence_dir.resolve()
    if args.private_worker:
        run_worker(args)
        return
    args.evidence_dir.mkdir(mode=0o700, parents=True, exist_ok=False)
    # KWin's DBus and Wayland sockets have short sockaddr_un path limits.
    with tempfile.TemporaryDirectory(prefix="nkm-x11-", dir="/tmp") as directory:
        private_root = pathlib.Path(directory)
        env = private_environment(private_root)
        env["NEKOMOTE_X11_PRIVATE_ROOT"] = str(private_root)
        session = private_root / "session.sh"
        worker = [sys.executable, str(pathlib.Path(__file__).resolve()), *sys.argv[1:],
                  "--private-worker", "--private-root", str(private_root)]
        session.write_text("#!/bin/sh\nexec " + shlex.join(worker) + "\n", encoding="utf-8")
        session.chmod(0o700)
        lock_path = REPO / "build/gpu-validation.lock"
        lock_path.parent.mkdir(parents=True, exist_ok=True)
        with lock_path.open("a") as lock, (args.evidence_dir / "kwin.log").open("w") as log:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            command = ["dbus-run-session", "--", str(args.compositor), "--virtual", "--xwayland",
                       "--width", "640", "--height", "480", "--socket", "nekomote-x11-test",
                       "--no-lockscreen", "--no-global-shortcuts", "--no-kactivities",
                       "--exit-with-session", str(session)]
            (args.evidence_dir / "command.json").write_text(json.dumps(command, indent=2) + "\n")
            process = subprocess.Popen(command, env=env, start_new_session=True,
                stdin=subprocess.DEVNULL, stdout=log, stderr=log)
            try:
                code = process.wait(timeout=args.timeout * 35 + 30)
            finally:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
                shutil.copytree(private_root / "config", args.evidence_dir / "private-config")
            result_path = args.evidence_dir / "results.json"
            if not result_path.exists():
                result_path.write_text(json.dumps(dict(passed=False,
                    error=f"private session exited {code} without worker results; see kwin.log"),
                    indent=2) + "\n")
                raise SystemExit(1)
            if code or not json.loads(result_path.read_text()).get("passed"):
                raise SystemExit(code or 1)


if __name__ == "__main__":
    main()
