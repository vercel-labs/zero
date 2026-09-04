#!/usr/bin/env python3
"""
End-to-end test for the 0pm client against a live zero-pm registry.

Flow:
  1. build the 0pm executable with the zero compiler (direct backend)
  2. start the FastAPI registry and a static tarball server on free ports
  3. publish fixture packages (zero-http@1.0.0/1.2.0, zero-json@0.3.1, and a
     deliberately corrupt bad-sum@1.0.0)
  4. drive every CLI command in a scratch project:
     init / add (pinned + latest) / list / info / update / remove / install
  5. assert manifests, lockfiles, cache extraction, checksum rejection, and
     manifest rollback on failed add
  6. `zero check` + `zero run` the installed (extracted) package

Run:  python3 zero-pm/test_e2e.py
Exit code 0 == all assertions passed.

Prereqs: registry/requirements.txt installed, libcurl + zlib dev libraries,
and a built native compiler (make -C native/zero-c).
"""

from __future__ import annotations

import contextlib
import functools
import hashlib
import http.server
import json
import os
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent          # zero-pm/
ROOT = HERE.parent                              # repo root
ZERO = ROOT / "bin" / "zero"


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _wait_http(url: str, timeout: float = 15.0) -> None:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as r:
                if r.status < 500:
                    return
        except Exception as e:  # noqa: BLE001
            last = e
            time.sleep(0.1)
    raise RuntimeError(f"server at {url} never came up: {last}")


@contextlib.contextmanager
def registry_server(data_file: Path):
    sys.path.insert(0, str(ROOT / "registry"))
    os.environ["REGISTRY_DATA_FILE"] = str(data_file)
    os.environ["REGISTRY_TOKEN"] = ""
    import importlib

    import main as main_module
    main_module = importlib.reload(main_module)

    import uvicorn
    port = _free_port()
    config = uvicorn.Config(main_module.app, host="127.0.0.1", port=port, log_level="warning")
    server = uvicorn.Server(config)
    thread = threading.Thread(target=server.run, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{port}"
    try:
        _wait_http(base + "/v1/health")
        yield base
    finally:
        server.should_exit = True
        thread.join(timeout=5)


@contextlib.contextmanager
def static_file_server(directory: Path):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(directory))
    port = _free_port()
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{port}"
    finally:
        httpd.shutdown()
        thread.join(timeout=5)


def make_pkg(root: Path, name: str, version: str) -> tuple[Path, str]:
    src = root / "pkg-src" / f"{name}-{version}"
    (src / "src").mkdir(parents=True, exist_ok=True)
    (src / "zero.json").write_text(json.dumps({
        "package": {"name": name, "version": version, "license": "MIT"},
        "targets": {"cli": {"kind": "exe", "main": "src/lib.0"}},
    }, indent=2), encoding="utf-8")
    (src / "src" / "lib.0").write_text(
        f"// {name} {version}\n"
        "pub fn answer() -> u32 {\n    return 42_u32\n}\n\n"
        "pub fn main(world: World) -> Void raises {\n"
        '    if answer() == 42_u32 {\n        check world.out.write("ok\\n")\n    }\n}\n',
        encoding="utf-8",
    )
    tar_path = root / "dist" / f"{name}-{version}.tar.gz"
    tar_path.parent.mkdir(exist_ok=True)
    with tarfile.open(tar_path, "w:gz") as t:
        t.add(src, arcname=src.name)
    return tar_path, "sha256:" + hashlib.sha256(tar_path.read_bytes()).hexdigest()


def publish(reg: str, name: str, version: str, url: str, checksum: str) -> None:
    body = json.dumps({
        "name": name, "version": version, "download_url": url,
        "checksum": checksum, "description": f"test package {name}",
        "deps": [], "token": "",
    }).encode()
    req = urllib.request.Request(reg + "/v1/packages", data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as r:
        assert r.status == 201, r.status


def check(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)
    print(f"  ok: {msg}")


def main() -> int:
    print("zero-pm client end-to-end test")

    print("[0] build 0pm with the zero compiler")
    subprocess.run(["make", "-C", str(HERE / "native")], check=True, capture_output=True)
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        zpm = tmp / "0pm"
        proc = subprocess.run(
            [str(ZERO), "build", "--emit", "exe", str(HERE), "--out", str(zpm)],
            capture_output=True, text=True, cwd=str(ROOT),
        )
        check(proc.returncode == 0, f"0pm builds ({proc.stdout.strip()}{proc.stderr.strip()})")

        home = tmp / "home"
        home.mkdir()
        proj = tmp / "proj"
        proj.mkdir()

        with static_file_server(tmp / "dist") as files, \
                registry_server(tmp / "registry.json") as reg:

            print("[1] publish fixture packages")
            for name, ver in [("zero-http", "1.0.0"), ("zero-http", "1.2.0"), ("zero-json", "0.3.1")]:
                tar_path, checksum = make_pkg(tmp, name, ver)
                publish(reg, name, ver, f"{files}/{tar_path.name}", checksum)
            publish(reg, "bad-sum", "1.0.0", f"{files}/zero-http-1.0.0.tar.gz", "sha256:" + "0" * 64)
            check(True, "published zero-http@1.0.0/1.2.0, zero-json@0.3.1, bad-sum@1.0.0")

            env = dict(os.environ, ZERO_REGISTRY=reg, HOME=str(home))

            def zpm_run(*args: str) -> str:
                p = subprocess.run([str(zpm), *args], capture_output=True, text=True,
                                   cwd=str(proj), env=env, timeout=60)
                return p.stdout + p.stderr

            print("[2] init")
            out = zpm_run("init", "my-app")
            check("created zero.json" in out, "init creates zero.json")
            manifest = json.loads((proj / "zero.json").read_text())
            check(manifest["package"]["name"] == "my-app", "manifest has package name")

            print("[3] add pinned version")
            out = zpm_run("add", "zero-http@1.0.0")
            check("zero-http@1.0.0" in out and "added zero-http" in out, "add zero-http@1.0.0")
            manifest = json.loads((proj / "zero.json").read_text())
            check(manifest["dependencies"]["zero-http"] == "1.0.0", "manifest pins 1.0.0")
            lock = json.loads((proj / ".zero" / "zero.lock.json").read_text())
            check(lock["packages"]["zero-http"]["version"] == "1.0.0", "lockfile pins 1.0.0")
            check(lock["packages"]["zero-http"]["checksum"].startswith("sha256:"), "lockfile has checksum")

            print("[4] cache extraction (tar.gz)")
            cache_pkg = home / ".zero" / "packages" / "zero-http" / "1.0.0" / "zero-http-1.0.0"
            check((cache_pkg / "zero.json").exists(), "tarball extracted: zero.json present")
            check((cache_pkg / "src" / "lib.0").exists(), "tarball extracted: src/lib.0 present")
            check((cache_pkg.parent / ".zpm-ok").exists(), "cache marker written")

            proc = subprocess.run([str(ZERO), "check", str(cache_pkg)],
                                  capture_output=True, text=True, cwd=str(ROOT))
            check(proc.returncode == 0, "`zero check` passes on installed package")
            proc = subprocess.run([str(ZERO), "run", str(cache_pkg)],
                                  capture_output=True, text=True, cwd=str(ROOT))
            check(proc.returncode == 0 and "ok" in proc.stdout, "`zero run` executes installed package")

            print("[5] add latest")
            out = zpm_run("add", "zero-json")
            check("zero-json@0.3.1" in out, "add resolves latest from registry")

            print("[6] list / info")
            out = zpm_run("list")
            check("zero-http@1.0.0" in out and "zero-json@0.3.1" in out, "list shows both packages")
            out = zpm_run("info", "zero-http")
            check("status: installed" in out, "info reports installed package")
            out = zpm_run("info", "nope")
            check("package not found" in out, "info rejects unknown package")

            print("[7] update")
            out = zpm_run("update")
            check("updating zero-http to 1.2.0" in out, "update bumps to latest")
            manifest = json.loads((proj / "zero.json").read_text())
            check(manifest["dependencies"]["zero-http"] == "1.2.0", "manifest updated to 1.2.0")
            out = zpm_run("update")
            check("all packages are up to date" in out, "second update is a no-op")

            print("[8] checksum verification")
            out = zpm_run("add", "bad-sum")
            check("failed to install" in out, "corrupt checksum rejected")
            check("zero.json restored" in out, "manifest rolled back after failed add")
            manifest = json.loads((proj / "zero.json").read_text())
            check("bad-sum" not in manifest.get("dependencies", {}), "bad-sum absent from manifest")
            check(not (home / ".zero" / "packages" / "bad-sum").exists(), "bad-sum not cached")

            print("[9] remove")
            out = zpm_run("remove", "zero-json")
            check("removed zero-json" in out, "remove reports success")
            manifest = json.loads((proj / "zero.json").read_text())
            check("zero-json" not in manifest["dependencies"], "manifest dependency removed")
            lock = json.loads((proj / ".zero" / "zero.lock.json").read_text())
            check("zero-json" not in lock["packages"], "lock entry removed")
            out = zpm_run("remove", "nope")
            check("package not found in dependencies" in out, "remove rejects unknown package")

            print("[10] install is idempotent")
            out = zpm_run("install")
            check("packages installed successfully" in out, "install succeeds from lock state")

    print("\nALL CLIENT E2E CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
