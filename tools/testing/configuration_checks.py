"""Opt-in normal-startup checks shared by the private compositor harnesses."""
import hashlib
import subprocess
import time


def check_profile_lifecycle(binary, backend, assets, root, env, marker, timeout):
    """Save explicitly, start twice, and assert no automatic panel/launch writes."""
    profile = root / f"profile-{backend}.json"
    state = profile.with_name(profile.stem + ".panel.json")
    command = [str(binary), "--config", str(profile), "--assets", str(assets),
               "--backend", backend, "--save-config"]
    saved = subprocess.run(command, env=env, capture_output=True, text=True, timeout=timeout,
                           stdin=subprocess.DEVNULL)
    (root / f"profile-{backend}-save.log").write_text(saved.stdout + saved.stderr)
    if saved.returncode or not profile.is_file() or state.exists():
        raise RuntimeError(f"{backend}: explicit profile save failed or wrote panel data")
    original = profile.read_bytes()
    cases = []
    for existing in (False, True):
        # The second normal startup must not rewrite even formatting of user data.
        panel = b'{ "version": 1, "fps": 30, "mute": true, "language": "ja" }\n'
        if existing:
            state.write_bytes(panel)
        label = f"profile-{backend}-{'existing' if existing else 'first'}"
        path = root / (label + ".log")
        with path.open("w") as log:
            client = subprocess.Popen([str(binary), "--config", str(profile), "--mute", "--ui-open"],
                                      env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + timeout
                while marker not in path.read_text():
                    if client.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"{label}: saved-profile startup failed")
                    time.sleep(0.05)
                time.sleep(2)
                if client.poll() is not None:
                    raise RuntimeError(f"{label}: normal playback exited early")
                client.terminate()
                if client.wait(timeout=timeout):
                    raise RuntimeError(f"{label}: unclean normal playback shutdown")
            finally:
                if client.poll() is None:
                    client.kill()
                    client.wait(timeout=timeout)
        if profile.read_bytes() != original:
            raise RuntimeError(f"{label}: normal playback rewrote launch configuration")
        if existing:
            if state.read_bytes() != panel:
                raise RuntimeError(f"{label}: normal playback rewrote panel preferences")
        elif state.exists():
            raise RuntimeError(f"{label}: normal playback saved default panel preferences")
        text = path.read_text()
        if env.get("NEKOMOTE_VK_VALIDATION") == "1" and backend == "vulkan":
            if "[validation]" in text or "validation errors=0 warnings=0" not in text:
                raise RuntimeError(f"{label}: Vulkan validation failed")
        cases.append(dict(name=label, passed=True, launch_sha256=hashlib.sha256(original).hexdigest(),
                          existing_panel=existing))
    return cases
