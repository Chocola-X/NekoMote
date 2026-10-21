#!/usr/bin/env python3
"""Opt-in layer-shell GPU smoke tests in a private virtual KWin session.

No default display connection, desktop configuration or system input is used.
Every binary is explicit. The outer process owns a bounded process group and
the project GPU lock; all evidence and synthetic assets remain in a new folder.
This tests layer-shell protocol behavior on KWin, not other compositors.
"""
import argparse
import contextlib
import fcntl
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time

from configuration_checks import check_profile_lifecycle

REPO = pathlib.Path(__file__).resolve().parents[2]


def private_environment(root):
    """Retain driver/tool lookup, replace every session and user data path."""
    env = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET", "XAUTHORITY",
                "DBUS_SESSION_BUS_ADDRESS", "DBUS_SESSION_BUS_PID",
                "SESSION_MANAGER", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH",
                "QML_IMPORT_PATH", "QT_WAYLAND_RECONNECT", "XDG_SESSION_ID",
                "XDG_SEAT", "XDG_VTNR", "KDE_FULL_SESSION", "DESKTOP_SESSION"):
        env.pop(key, None)
    for variable, name in (("HOME", "home"), ("XDG_RUNTIME_DIR", "runtime"),
                           ("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                           ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state")):
        directory = root / name
        directory.mkdir(mode=0o700)
        env[variable] = str(directory)
    env.update(XDG_CONFIG_DIRS="/etc/xdg", XDG_DATA_DIRS="/usr/local/share:/usr/share",
               XDG_SESSION_TYPE="wayland", QT_QPA_PLATFORM="offscreen",
               XDG_CURRENT_DESKTOP="NekoMote-Test", QT_FORCE_STDERR_LOGGING="1",
               KWIN_NO_GLOBAL_SHORTCUTS="1")
    return env


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


@contextlib.contextmanager
def server(command, env, log_path, socket_name, timeout):
    with log_path.open("w") as log:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=log)
        try:
            socket = pathlib.Path(env["XDG_RUNTIME_DIR"]) / socket_name
            deadline = time.monotonic() + timeout
            while not socket.is_socket():
                if process.poll() is not None:
                    raise RuntimeError(f"server exited {process.returncode}: {log_path}")
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"server startup timeout: {log_path}")
                time.sleep(0.05)
            yield process
        finally:
            stop(process)


def read_ppm(path):
    data = path.read_bytes()
    # The diagnostic writer uses exactly these three P6 header lines.
    magic, dimensions, maximum, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maximum != b"255":
        raise RuntimeError(f"invalid PPM header: {path}")
    width, height = (int(value) for value in dimensions.split())
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise RuntimeError(f"truncated or invalid PPM: {path}")
    return width, height, pixels


def write_fixture(root, importer, timeout):
    path = REPO / "tools/reference_capture/synthetic_model.py"
    spec = importlib.util.spec_from_file_location("nekomote_synthetic", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    source = root / "synthetic-source"
    source.mkdir()
    # The shared wallpaper starts at origin (0, 400), intended for full-height
    # characters. Offset this 32px synthetic tile to remain inside a small output.
    document = module.model([module.sprite(-16, -416)])
    document["metadata"]["timelineControl"] = [dict(
        label="idle", diff=0, lastTime=60, loopBegin=0, loopEnd=60, variableList=[])]
    (source / "model.json").write_text(json.dumps(document), encoding="utf-8")
    (source / "model.resx.json").write_text(json.dumps(dict(
        PsbVersion=4, PsbType="Motion", Platform="ems", Resources={"0": "texture.png"})),
        encoding="utf-8")
    rgba = bytes((247, 23, 191, 255)) * (32 * 32)
    (source / "texture.png").write_bytes(module.png_rgba(32, 32, rgba))
    model_dir = root / "synthetic-model"
    result = subprocess.run([str(importer), str(source / "model.json"),
                             str(source / "model.resx.json"), "--output", str(model_dir)],
                            capture_output=True, text=True, timeout=timeout,
                            stdin=subprocess.DEVNULL)
    (root / "import.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError("synthetic model import failed; see import.log")
    session = dict(schema="nekomote.session", version=1, session=dict(
        model="synthetic-layer-shell", idle_timeline="idle", talk_max=5,
        interaction=dict(eye=None, regions={name: None for name in (
            "bust", "eye", "face", "head", "pant")}), reactions=[]))
    session_path = root / "session.nks.json"
    session_path.write_text(json.dumps(session), encoding="utf-8")
    catalog_model = root / "assets/characters/synthetic/default"
    shutil.copytree(model_dir, catalog_model)
    shutil.copyfile(session_path, catalog_model / "session.nks.json")
    return model_dir / "model.nkm", session_path


def run_worker(args):
    root = args.evidence_dir
    env = dict(os.environ)
    if pathlib.Path(env.get("XDG_RUNTIME_DIR", "")) != root / "runtime":
        raise RuntimeError("worker requires its private runtime directory")
    if not env.get("DBUS_SESSION_BUS_ADDRESS"):
        raise RuntimeError("worker requires a private dbus-run-session")
    result = dict(scope="KWin virtual output layer-shell protocol/GPU smoke",
                  binary=str(args.binary), backend=args.backend,
                  binary_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  compositor=str(args.compositor), cases=[], passed=False,
                  environment={key: env.get(key) for key in (
                      "LIBGL_ALWAYS_SOFTWARE", "__EGL_VENDOR_LIBRARY_FILENAMES", "KWIN_COMPOSE")},
                  not_covered=["Sway", "Hyprland", "niri", "physical outputs",
                               "real input", "hotplug", "physical mode resize while running",
                               "scale change while running",
                               "real-model animation", "performance", "long-term",
                               "compositor final output pixels"],
                  optional_checks={"runtime_scale": dict(
                      executed=False, reason="--output-tool was not supplied")})
    model, session = write_fixture(root, args.importer, args.timeout)
    result["inputs_sha256"] = {str(path.relative_to(root)):
        hashlib.sha256(path.read_bytes()).hexdigest() for path in (
            model, session, root / "synthetic-source/model.json",
            root / "synthetic-source/model.resx.json", root / "synthetic-source/texture.png")}
    result["compositor_runs"] = []

    def validate_log(name, log_text):
        if args.backend == "vulkan" and env.get("NEKOMOTE_VK_VALIDATION") == "1":
            if "[validation]" in log_text or "validation errors=0 warnings=0" not in log_text:
                raise RuntimeError(f"{name}: validation errors/warnings or missing validation summary")

    def check(name, command, *, client_env=None, success=True, text=None):
        completed = subprocess.run(command, env=client_env or env, capture_output=True,
                                   text=True, timeout=args.timeout, stdin=subprocess.DEVNULL)
        (root / f"{name}.log").write_text(completed.stdout + completed.stderr,
                                         encoding="utf-8")
        if (completed.returncode == 0) != success:
            raise RuntimeError(f"{name}: unexpected exit {completed.returncode}; see log")
        if success and "--list-outputs" not in command:
            validate_log(name, completed.stdout + completed.stderr)
        if text and text not in completed.stdout + completed.stderr:
            raise RuntimeError(f"{name}: expected diagnostic {text!r}; see log")
        result["cases"].append(dict(name=name, command=command,
                                   exit_code=completed.returncode, passed=True))
        return completed

    common = [str(args.binary), "--backend", args.backend, "--model", str(model), "--session", str(session),
              "--config", str(root / "wallpaper.json")]

    def runtime_scale(client_env, output, width, height):
        """Change only the private KWin output while the owned client is alive."""
        status = result["optional_checks"]["runtime_scale"]
        status.update(executed=True, passed=False)
        status.pop("reason", None)
        capture = root / "runtime-scale.ppm"
        log_path = root / "runtime-scale.log"
        command = common + ["--output", output, "--input", "passthrough", "--frames", "120",
                            "--capture", str(capture)]
        tool_runs = []
        status["output_tool_runs"] = tool_runs

        def change_scale(scale):
            tool_command = [str(args.output_tool), f"output.{output}.scale.{scale}"]
            # KScreen's offscreen Qt backend has no outputs and can still return
            # success for an unknown output. Bind it to this private Wayland
            # socket explicitly; never inherit the harness's offscreen setting.
            tool_env = dict(client_env, QT_QPA_PLATFORM="wayland")
            completed = subprocess.run(tool_command, env=tool_env, capture_output=True,
                                       text=True, timeout=args.timeout,
                                       stdin=subprocess.DEVNULL)
            tool_log = completed.stdout + completed.stderr
            (root / f"runtime-scale-tool-{scale}.log").write_text(
                tool_log, encoding="utf-8")
            tool_runs.append(dict(command=tool_command, exit_code=completed.returncode))
            if completed.returncode or re.search(
                    r"not found|no outputs?|failed|error", tool_log, re.IGNORECASE):
                raise RuntimeError(f"runtime scale: output tool scale {scale} failed; see log")

        with log_path.open("w") as log:
            client = subprocess.Popen(command, env=dict(client_env, WAYLAND_DEBUG="client"),
                                      stdin=subprocess.DEVNULL,
                                      stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + args.timeout
                while " background " not in log_path.read_text():
                    if client.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("runtime scale client did not initialize")
                    time.sleep(0.05)
                # Let the first buffers present before changing the live output.
                time.sleep(0.2)
                if client.poll() is not None:
                    raise RuntimeError("runtime scale client exited before scale change")
                change_scale(2)
                code = client.wait(timeout=args.timeout)
                if code:
                    raise RuntimeError(f"runtime scale client exited {code}; see log")
                log_text = log_path.read_text()
                validate_log("runtime-scale", log_text)
                if not re.search(r"wl_output[@#]\d+\.scale\(2\)", log_text):
                    raise RuntimeError("runtime scale: client did not receive wl_output.scale(2)")
                if not re.search(r"wl_surface[@#]\d+\.set_buffer_scale\(2\)", log_text):
                    raise RuntimeError("runtime scale: client did not send set_buffer_scale(2)")
                status["layer_configure_ack_observed"] = bool(
                    re.search(r"zwlr_layer_surface_v1[@#]\d+\.configure\(", log_text) and
                    re.search(r"zwlr_layer_surface_v1[@#]\d+\.ack_configure\(", log_text))
                w, h, pixels = read_ppm(capture)
                if (w, h) != (width, height):
                    raise RuntimeError(f"runtime scale: expected {width}x{height} physical capture, "
                                       f"got {w}x{h}")
                colored = sum(1 for i in range(0, len(pixels), 3)
                              if all(abs(pixels[i + j] - c) <= 2
                                     for j, c in enumerate((247, 23, 191))))
                if colored < 4:
                    raise RuntimeError("runtime scale: synthetic material absent from capture")
                result["cases"].append(dict(name="runtime-scale", command=command,
                    exit_code=code, passed=True, width=w, height=h, material_pixels=colored,
                    pixels_sha256=hashlib.sha256(pixels).hexdigest()))
                status.update(passed=True, width=w, height=h)
                result["not_covered"].remove("scale change while running")
            finally:
                stop(client)
                # Both the D-Bus bus and Wayland socket belong to this harness;
                # restore its baseline even when the client or resize fails.
                change_scale(1)

    try:
        missing_env = dict(env, WAYLAND_DISPLAY="nekomote-nonexistent")
        check("missing-display", common + ["--frames", "1"],
              client_env=missing_env, success=False)
        socket_name = "nekomote-no-protocol"
        with server([str(args.no_protocol_server), env["XDG_RUNTIME_DIR"], socket_name], env,
                    root / "no-protocol-server.log", socket_name, args.timeout):
            check("missing-layer-shell", common + ["--frames", "1"],
                  client_env=dict(env, WAYLAND_DISPLAY=socket_name), success=False,
                  text="layer-shell")

        scenarios = [("base", 320, 240, 1), ("resized", 400, 300, 1)]
        if args.real_assets:
            scenarios.append(("desktop", 1280, 720, 1))
        scenarios.append(("scaled", 320, 240, 2))
        for label, width, height, scale in scenarios:
            socket_name = "nekomote-" + label
            command = [str(args.compositor), "--virtual", "--width", str(width),
                       "--height", str(height), "--scale", str(scale), "--output-count", "2",
                       "--socket", socket_name, "--no-lockscreen", "--no-global-shortcuts",
                       "--no-kactivities"]
            result["compositor_runs"].append(command)
            if label == "scaled":
                # Change only the private KWin's saved output configuration.
                # --scale alone enlarges virtual modes on some KWin versions
                # without advertising wl_output.scale=2 to clients.
                config_path = pathlib.Path(env["XDG_CONFIG_HOME"]) / "kwinoutputconfig.json"
                configuration = json.loads(config_path.read_text())
                for group in configuration:
                    if group.get("name") == "outputs":
                        for output in group["data"]:
                            output["scale"] = 2
                config_path.write_text(json.dumps(configuration), encoding="utf-8")
            with server(command, env, root / f"kwin-{label}.log", socket_name,
                        args.timeout) as compositor:
                client_env = dict(env, WAYLAND_DISPLAY=socket_name)
                listing = check(f"outputs-{label}", [str(args.binary), "--list-outputs"],
                                client_env=client_env)
                outputs = listing.stdout.splitlines()
                if len(outputs) != 2 or len(set(outputs)) != 2 or not all(outputs):
                    raise RuntimeError(f"expected two distinct output names: {outputs!r}")
                if label == "base":
                    check("unknown-output", common + ["--output", "NEKOMOTE-MISSING",
                                                       "--frames", "1"],
                          client_env=client_env, success=False)
                captures = []
                modes = ("passthrough", "interactive") if label == "base" else ("passthrough",)
                for index, mode in enumerate(modes):
                    name = f"{label}-{mode}"
                    capture = root / f"{name}.ppm"
                    completed = check(name, common + ["--output", outputs[index], "--input", mode,
                                         "--frames", "4", "--capture", str(capture)],
                          client_env=client_env)
                    if label == "scaled":
                        if ", scale 2," not in completed.stderr:
                            raise RuntimeError("configured output did not reach buffer scale 2")
                    w, h, pixels = read_ppm(capture)
                    if (w, h) != (width * scale, height * scale):
                        raise RuntimeError(f"{name}: incorrect physical output size {w}x{h}")
                    # The opaque synthetic material must appear, independent of UI text.
                    colored = sum(1 for i in range(0, len(pixels), 3)
                                  if all(abs(pixels[i + j] - c) <= 2
                                         for j, c in enumerate((247, 23, 191))))
                    if colored < 4:
                        raise RuntimeError(f"{name}: synthetic material absent from capture")
                    captures.append(pixels)
                    result["cases"][-1].update(width=w, height=h, material_pixels=colored,
                        pixels_sha256=hashlib.sha256(pixels).hexdigest())
                    if args.real_assets and label == "desktop" and index == 0:
                        capture = root / "real-assets.ppm"
                        check("real-assets", [str(args.binary), "--backend", args.backend, "--assets", str(args.real_assets),
                              "--config", str(root / "real-wallpaper.json"), "--output", outputs[0],
                              "--frames", "4", "--capture", str(capture)], client_env=client_env)
                        w, h, pixels = read_ppm(capture)
                        if len(set(zip(pixels[::3], pixels[1::3], pixels[2::3]))) < 16:
                            raise RuntimeError("real assets produced an empty/uniform image")
                        result["cases"][-1].update(width=w, height=h,
                            pixels_sha256=hashlib.sha256(pixels).hexdigest())
                        capture = root / "real-assets-ui.ppm"
                        check("real-assets-ui", [str(args.binary), "--backend", args.backend,
                              "--assets", str(args.real_assets), "--ui-open",
                              "--config", str(root / "real-wallpaper.json"), "--output", outputs[0],
                              "--frames", "4", "--capture", str(capture)], client_env=client_env)
                if label == "base":
                    result["cases"].extend(check_profile_lifecycle(
                        args.binary, args.backend, args.real_assets or root / "assets", root,
                        client_env, " background ", args.timeout))
                    if args.output_tool:
                        runtime_scale(client_env, outputs[0], width, height)
                    if args.backend == "vulkan" and args.retry_preload:
                        capture = root / "present-retry.ppm"
                        retry_env = dict(client_env, LD_PRELOAD=str(args.retry_preload),
                                         NEKOMOTE_TEST_PRESENT_RETRY="1")
                        check("present-retry", common + ["--output", outputs[0], "--frames", "4",
                              "--capture", str(capture)], client_env=retry_env,
                              text="injected OUT_OF_DATE without surface commit")
                        if read_ppm(capture)[2] != captures[0]:
                            raise RuntimeError("present retry advanced or corrupted the fixed frame")
                    existing = root / "existing-capture.ppm"
                    original = b"preserve existing capture\x00\xff"
                    existing.write_bytes(original)
                    check("existing-capture", common + ["--frames", "1", "--capture",
                          str(existing)], client_env=client_env, success=False)
                    if existing.read_bytes() != original:
                        raise RuntimeError("capture overwrote an existing file")
                    capture = root / "base-repeat.ppm"
                    check("base-repeat", common + ["--output", outputs[0], "--frames", "4",
                          "--capture", str(capture)], client_env=client_env)
                    if read_ppm(capture)[2] != captures[0]:
                        raise RuntimeError("identical fixed-step runs produced different pixels")
                    capture = root / "assets-catalog.ppm"
                    check("assets-catalog", [str(args.binary), "--backend", args.backend, "--assets", str(root / "assets"),
                          "--config", str(root / "wallpaper.json"), "--output", outputs[0],
                          "--frames", "4", "--capture", str(capture)], client_env=client_env)
                    w, h, pixels = read_ppm(capture)
                    colored = sum(1 for i in range(0, len(pixels), 3)
                                  if all(abs(pixels[i + j] - c) <= 2
                                         for j, c in enumerate((247, 23, 191))))
                    if colored < 4:
                        raise RuntimeError("asset catalog did not render synthetic material")
                    result["cases"][-1].update(width=w, height=h, material_pixels=colored,
                        pixels_sha256=hashlib.sha256(pixels).hexdigest())
                    # Terminate only the compositor this harness owns. This checks
                    # connection-loss cleanup without sending desktop input events.
                    log_path = root / "compositor-disconnect.log"
                    with log_path.open("w") as log:
                        client = subprocess.Popen(common + ["--frames", "100000"],
                                                  env=client_env, stdin=subprocess.DEVNULL,
                                                  stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + args.timeout
                            while " background " not in log_path.read_text():
                                if client.poll() is not None or time.monotonic() >= deadline:
                                    raise RuntimeError("disconnect client did not initialize")
                                time.sleep(0.05)
                            stop(compositor)
                            code = client.wait(timeout=args.timeout)
                            if code != 1:
                                raise RuntimeError(f"compositor disconnect exit was {code}, not 1")
                            result["cases"].append(dict(name="compositor-disconnect",
                                                       exit_code=code, passed=True))
                        finally:
                            stop(client)
                if label == "scaled":
                    log_path = root / "signal-cleanup.log"
                    with log_path.open("w") as log:
                        client = subprocess.Popen(common + ["--mute", "--no-save"],
                                                  env=client_env, stdin=subprocess.DEVNULL,
                                                  stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + args.timeout
                            while " background " not in log_path.read_text():
                                if client.poll() is not None or time.monotonic() >= deadline:
                                    raise RuntimeError("signal client did not initialize")
                                time.sleep(0.05)
                            client.terminate()
                            code = client.wait(timeout=args.timeout)
                            if code:
                                raise RuntimeError(f"SIGTERM cleanup failed with exit {code}")
                            result["cases"].append(dict(name="signal-cleanup", exit_code=code,
                                                       passed=True))
                        finally:
                            stop(client)
        if any((root / name).exists() for name in ("wallpaper.json", "real-wallpaper.json", "wallpaper.panel.json", "real-wallpaper.panel.json")):
            raise RuntimeError("diagnostic fixed-step execution wrote persistent config")
        result["passed"] = True
    except Exception as error:
        result["error"] = str(error)
        raise
    finally:
        (root / "results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


def executable(value):
    path = pathlib.Path(value).resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise argparse.ArgumentTypeError(f"not an executable file: {path}")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=executable)
    parser.add_argument("--backend", choices=("opengl", "vulkan"), default="opengl")
    parser.add_argument("--retry-preload", type=lambda value: pathlib.Path(value).resolve(strict=True),
                        help="optional diagnostic present OUT_OF_DATE interposer (Vulkan only)")
    parser.add_argument("--importer", required=True, type=executable)
    parser.add_argument("--compositor", required=True, type=executable)
    parser.add_argument("--output-tool", type=executable,
                        help="optional kscreen-doctor executable for private live-output scale")
    parser.add_argument("--no-protocol-server", required=True, type=executable)
    parser.add_argument("--real-assets", type=lambda value: pathlib.Path(value).resolve(strict=True),
                        help="optional read-only real asset directory")
    parser.add_argument("--evidence-dir", required=True, type=pathlib.Path)
    parser.add_argument("--timeout", type=int, default=30,
                        help="timeout per process/startup, in seconds (1..120)")
    parser.add_argument("--private-worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1 <= args.timeout <= 120:
        parser.error("--timeout must be between 1 and 120 seconds")
    args.evidence_dir = args.evidence_dir.resolve()
    if args.private_worker:
        run_worker(args)
        return
    args.evidence_dir.mkdir(mode=0o700, parents=True, exist_ok=False)
    env = private_environment(args.evidence_dir)
    lock_path = REPO / "build/gpu-validation.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        command = ["dbus-run-session", "--", sys.executable, str(pathlib.Path(__file__).resolve()),
                   *sys.argv[1:], "--private-worker"]
        process = subprocess.Popen(command, env=env, start_new_session=True,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(timeout=args.timeout * 20 + 30)
        finally:
            # dbus, compositor and client descendants share this dedicated group.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        if code:
            raise SystemExit(code)


if __name__ == "__main__":
    main()
