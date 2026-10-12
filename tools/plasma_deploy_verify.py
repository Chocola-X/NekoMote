#!/usr/bin/env python3
"""G27 independent dynamic host verification.

Installs the Plasma wallpaper to a temporary XDG root, then runs the package
probe with QML_IMPORT_PATH pointing to the install directory (not build tree)
and NO LD_LIBRARY_PATH. The probe loads the QML module and native library via
relative RPATH alone, proving the deployment is self-contained.

Sequence: stage-verify → install → status → dynamic-load → uninstall → no-residual

Usage:
  plasma_deploy_verify.py [--stage DIR] [--probe BIN] [--model M] [--session S]
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
DEFAULT_STAGE = REPO / "build/plasma/adapters/plasma/stage"
DEFAULT_PROBE = REPO / "build/plasma/bin/nekomote_plasma_package_probe"
DEFAULT_MODEL = REPO / "assets/characters/coconut/casual/model.nkm.json"
DEFAULT_SESSION = REPO / "assets/characters/coconut/casual/session.nks.json"


def run(cmd, **kw) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def fail(msg: str) -> int:
    print(f"FAIL: {msg}", file=sys.stderr)
    return 1


def main() -> int:
    stage = Path(os.environ.get("NEKOMOTE_STAGE", DEFAULT_STAGE))
    probe = Path(os.environ.get("NEKOMOTE_PROBE", DEFAULT_PROBE))
    model = Path(os.environ.get("NEKOMOTE_MODEL", DEFAULT_MODEL))
    session = Path(os.environ.get("NEKOMOTE_SESSION", DEFAULT_SESSION))
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--stage": stage = Path(args[i+1]); i += 2
        elif args[i] == "--probe": probe = Path(args[i+1]); i += 2
        elif args[i] == "--model": model = Path(args[i+1]); i += 2
        elif args[i] == "--session": session = Path(args[i+1]); i += 2
        else: i += 1

    if not stage.is_dir():
        return fail(f"stage not found: {stage}")
    if not probe.is_file():
        return fail(f"probe not found: {probe}")
    if not model.is_file() or not session.is_file():
        return fail(f"model/session not found: {model} / {session}")

    tmproot = Path(tempfile.mkdtemp(prefix="plasma-deploy-verify-"))
    evidence = tmproot / "evidence"
    evidence.mkdir()
    print(f"verify: tmproot={tmproot}")

    try:
        # 1. Stage verify
        print("\n[1/6] stage verify")
        r = run([sys.executable, str(REPO / "tools/plasma_stage_verify.py"), str(stage), str(probe)])
        if r.returncode != 0:
            return fail(f"stage verify failed:\n{r.stderr}")
        (evidence / "01_stage_verify.txt").write_text(r.stdout)

        # 2. Install to temp XDG root
        print("\n[2/6] install")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "--install", "--stage", str(stage), "--prefix", str(tmproot)])
        if r.returncode != 0:
            return fail(f"install failed:\n{r.stderr}")
        (evidence / "02_install.txt").write_text(r.stdout)

        # 3. Status
        print("\n[3/6] status")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "--status", "--prefix", str(tmproot)])
        if r.returncode != 0:
            return fail(f"status failed:\n{r.stderr}")
        (evidence / "03_status.txt").write_text(r.stdout)

        # 4. Dynamic load via probe (no LD_LIBRARY_PATH, QML_IMPORT_PATH from install)
        print("\n[4/6] dynamic host load")
        qml_path = tmproot / "nekomote" / "qml"
        probe_out = evidence / "04_probe"
        env = dict(os.environ)
        env.pop("LD_LIBRARY_PATH", None)
        env["QML_IMPORT_PATH"] = str(qml_path)
        env["XDG_DATA_HOME"] = str(tmproot)
        env["XDG_CONFIG_HOME"] = str(tmproot / "config")
        env["XDG_CACHE_HOME"] = str(tmproot / "cache")
        r = subprocess.run(
            [str(probe), "--lifecycle", "--model", str(model), "--session", str(session),
             "--output", str(probe_out), "--rounds", "3", "--config-rounds", "3"],
            capture_output=True, text=True, env=env, timeout=120
        )
        (evidence / "04_probe_stdout.txt").write_text(r.stdout)
        (evidence / "04_probe_stderr.txt").write_text(r.stderr)
        if r.returncode != 0:
            return fail(f"dynamic load failed (exit={r.returncode}):\n{r.stderr[-500:]}")
        if "lifecycle: configured checks passed" not in r.stdout:
            return fail("dynamic load: lifecycle marker not found")
        if not (probe_out / "lifecycle_final.png").is_file():
            return fail("dynamic load: lifecycle_final.png not produced")
        print(f"  dynamic load: lifecycle passed, png={probe_out / 'lifecycle_final.png'}")

        # 4b. Recovery test
        print("\n[4b/6] dynamic host recovery")
        probe_out2 = evidence / "04b_probe"
        r = subprocess.run(
            [str(probe), "--recovery", "--model", str(model), "--session", str(session),
             "--output", str(probe_out2)],
            capture_output=True, text=True, env=env, timeout=120
        )
        (evidence / "04b_probe_stdout.txt").write_text(r.stdout)
        if r.returncode != 0:
            return fail(f"recovery failed (exit={r.returncode}):\n{r.stderr[-500:]}")
        if "recovery: all bad->good->bad->good cycles passed" not in r.stdout:
            return fail("recovery: marker not found")
        print("  recovery: passed")

        # 5. Uninstall
        print("\n[5/6] uninstall")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "--uninstall", "--prefix", str(tmproot)])
        if r.returncode != 0:
            return fail(f"uninstall failed:\n{r.stderr}")
        (evidence / "05_uninstall.txt").write_text(r.stdout)

        # 6. No residual
        print("\n[6/6] no residual")
        nekomote_files = list((tmproot / "nekomote").rglob("*")) if (tmproot / "nekomote").exists() else []
        plasma_files = list((tmproot / "plasma").rglob("*")) if (tmproot / "plasma").exists() else []
        residual = [str(f) for f in nekomote_files + plasma_files if f.is_file()]
        if residual:
            return fail(f"residual files after uninstall: {residual}")
        print("  no residual files")

        # Save evidence to build
        build_evidence = REPO / "build/evidence/g27-deploy-verify"
        if build_evidence.exists():
            shutil.rmtree(build_evidence)
        shutil.copytree(evidence, build_evidence)
        print(f"\nPASS: all 6 steps passed, evidence saved to {build_evidence}")
        return 0
    except subprocess.TimeoutExpired:
        return fail("probe timed out")
    finally:
        shutil.rmtree(tmproot, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
