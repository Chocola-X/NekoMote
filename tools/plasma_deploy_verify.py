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

        # Keep installer-owned environment.d changes isolated from the user.
        deploy_env = dict(os.environ)
        deploy_env["XDG_DATA_HOME"] = str(tmproot)
        deploy_env["XDG_CONFIG_HOME"] = str(tmproot / "config")
        deploy_env["XDG_CACHE_HOME"] = str(tmproot / "cache")

        # 2. Install to temp XDG root
        print("\n[2/6] install")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "install", "--stage", str(stage)], env=deploy_env)
        if r.returncode != 0:
            return fail(f"install failed:\n{r.stderr}")
        (evidence / "02_install.txt").write_text(r.stdout)

        # 3. Status
        print("\n[3/6] status")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "status"], env=deploy_env)
        if r.returncode != 0:
            return fail(f"status failed:\n{r.stderr}")
        (evidence / "03_status.txt").write_text(r.stdout)

        # 4. Dynamic load via probe (no LD_LIBRARY_PATH; QML_IMPORT_PATH from install)
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

        # 4c. Reinstall exercises the same single deployment entry point.
        print("\n[4c/6] reinstall")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "reinstall", "--stage", str(stage)], env=deploy_env)
        if r.returncode != 0:
            return fail(f"reinstall failed:\n{r.stderr}")
        (evidence / "04c_reinstall.txt").write_text(r.stdout)

        # A pre-ID-change installation may have no deployment record. Reinstall
        # must remove that exact legacy KPackage after validating its metadata.
        print("\n[4d/6] legacy package migration")
        legacy_prefix = tmproot / "legacy-prefix"
        legacy_dir = legacy_prefix / "plasma/wallpapers/org.nekomote.wallpaper"
        legacy_dir.mkdir(parents=True)
        (legacy_dir / "metadata.json").write_text(json.dumps({
            "KPackageStructure": "Plasma/Wallpaper",
            "KPlugin": {"Id": "org.nekomote.wallpaper"},
        }))
        (legacy_dir / "legacy-marker").write_text("old plugin")
        legacy_env = dict(deploy_env)
        legacy_env["XDG_CONFIG_HOME"] = str(tmproot / "legacy-config")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "reinstall", "--stage", str(stage), "--prefix", str(legacy_prefix)],
                env=legacy_env)
        if r.returncode != 0:
            return fail(f"legacy reinstall failed:\n{r.stderr}")
        if legacy_dir.exists():
            return fail(f"legacy package directory remains after reinstall: {legacy_dir}")
        if not (legacy_prefix / "plasma/wallpapers/uk.nekopara.nekomote/metadata.json").is_file():
            return fail("legacy reinstall did not install the package under the new ID")
        (evidence / "04d_legacy_migration.txt").write_text(r.stdout)
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "uninstall", "--prefix", str(legacy_prefix)], env=legacy_env)
        if r.returncode != 0:
            return fail(f"legacy migration cleanup failed:\n{r.stderr}")

        # 5. Uninstall
        print("\n[5/6] uninstall")
        r = run([sys.executable, str(REPO / "tools/plasma_deploy.py"),
                 "uninstall"], env=deploy_env)
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
        environment_file = tmproot / "config/environment.d/nekomote.conf"
        if environment_file.exists():
            return fail(f"residual session environment after uninstall: {environment_file}")
        print("  no residual files")

        # Save evidence to build
        build_evidence = REPO / "build/evidence/g27-deploy-verify"
        if build_evidence.exists():
            shutil.rmtree(build_evidence)
        shutil.copytree(evidence, build_evidence)
        print(f"\nPASS: all deployment checks passed, evidence saved to {build_evidence}")
        return 0
    except subprocess.TimeoutExpired:
        return fail("probe timed out")
    finally:
        shutil.rmtree(tmproot, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
