"""
End-to-end integration test for the zero-pm registry.

Exercises the same protocol the zerolang `0pm` client (zero-pm/src/registry.0
and installer.0) speaks against a live server:

  1. start the FastAPI registry on a throwaway data file
  2. pack a sample zerolang package into a .tar.gz and compute its sha256
  3. serve the tarball over HTTP (the "download_url")
  4. publish name@version + download_url + checksum to the registry
  5. read it back via the agent JSON API (/v1/packages/...) exactly as
     registry.0 does, asserting every field the client parses is present
  6. download the tarball and verify the checksum matches — the same
     check installer.0 performs ("sha256:<hex>")
  7. publish a second version and confirm `latest` advances
  8. delete a version and confirm it is gone

Run:  python3 registry/test_integration.py
Exit code 0 == all assertions passed.
"""

from __future__ import annotations

import contextlib
import functools
import hashlib
import http.server
import os
import socket
import tarfile
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

import httpx
import uvicorn

HERE = Path(__file__).resolve().parent


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
def registry_server(data_file: Path, token: str = ""):
    """Run the FastAPI app in a background uvicorn thread."""
    # Import lazily with the registry dir on sys.path and env configured.
    import sys

    sys.path.insert(0, str(HERE))
    os.environ["REGISTRY_DATA_FILE"] = str(data_file)
    os.environ["REGISTRY_TOKEN"] = token
    import importlib

    import main as main_module

    main_module = importlib.reload(main_module)

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
    """Serve `directory` over HTTP (used as the package download host)."""
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


def pack_package(src_dir: Path, out_tar: Path) -> str:
    """Tar+gzip a package directory; return its 'sha256:<hex>' checksum."""
    with tarfile.open(out_tar, "w:gz") as tar:
        tar.add(src_dir, arcname=src_dir.name)
    digest = hashlib.sha256(out_tar.read_bytes()).hexdigest()
    return f"sha256:{digest}"


def make_sample_package(root: Path, name: str, version: str) -> Path:
    """Create a minimal zerolang package source tree on disk."""
    pkg = root / f"{name}-{version}"
    (pkg / "src").mkdir(parents=True, exist_ok=True)
    (pkg / "zero.json").write_text(
        '{\n'
        '  "package": { "name": "%s", "version": "%s", "license": "MIT" },\n'
        '  "targets": {\n'
        '    "cli": { "kind": "exe", "main": "src/lib.0", "devTarget": "host" }\n'
        '  },\n'
        '  "deps": {}\n'
        '}\n' % (name, version),
        encoding="utf-8",
    )
    (pkg / "src" / "lib.0").write_text(
        "// %s %s\n"
        "pub fn answer() -> u32 {\n"
        "    return 42_u32\n"
        "}\n\n"
        'test "answer is 42" {\n'
        "    expect answer() == 42_u32\n"
        "}\n" % (name, version),
        encoding="utf-8",
    )
    return pkg


def check(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)
    print(f"  ok: {msg}")


def main() -> int:
    print("zero-pm registry integration test")
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        data_file = tmp / "registry.json"
        dist = tmp / "dist"
        dist.mkdir()

        token = "secret-publish-token"

        # 1. pack a package
        print("[1] pack package zero-http@1.0.0")
        pkg_src = make_sample_package(tmp, "zero-http", "1.0.0")
        tar_path = dist / "zero-http-1.0.0.tar.gz"
        checksum = pack_package(pkg_src, tar_path)
        check(tar_path.exists(), f"tarball created ({tar_path.stat().st_size} bytes)")
        check(checksum.startswith("sha256:") and len(checksum) == 71, f"checksum {checksum}")

        with static_file_server(dist) as file_base, \
                registry_server(data_file, token=token) as reg:
            download_url = f"{file_base}/zero-http-1.0.0.tar.gz"
            client = httpx.Client(base_url=reg, timeout=10)

            # 2. publish (mirrors a future `0pm publish`)
            print("[2] publish to registry")
            r = client.post("/v1/packages", json={
                "name": "zero-http",
                "version": "1.0.0",
                "download_url": download_url,
                "checksum": checksum,
                "description": "Minimal HTTP helpers for zerolang",
                "deps": [],
                "token": token,
            })
            check(r.status_code == 201, f"publish returned 201 (got {r.status_code}: {r.text})")

            # publish without a token must be rejected
            r_bad = client.post("/v1/packages", json={
                "name": "x", "version": "1.0.0", "download_url": "http://x", "token": "wrong",
            })
            check(r_bad.status_code == 401, f"bad token rejected (got {r_bad.status_code})")

            # 3. read package info exactly as fetchPackageInfo() in registry.0
            print("[3] GET /v1/packages/zero-http  (fetchPackageInfo)")
            r = client.get("/v1/packages/zero-http")
            check(r.status_code == 200, "package info 200")
            info = r.json()
            check(info["name"] == "zero-http", "info.name parsed")
            check(info["latest"] == "1.0.0", "info.latest parsed")
            check(info["description"].startswith("Minimal HTTP"), "info.description parsed")

            # 4. read version info exactly as fetchVersionInfo() in registry.0
            print("[4] GET /v1/packages/zero-http/1.0.0  (fetchVersionInfo)")
            r = client.get("/v1/packages/zero-http/1.0.0")
            check(r.status_code == 200, "version info 200")
            ver = r.json()
            for field in ("name", "version", "download_url", "checksum"):
                check(field in ver, f"version.{field} present")
            check(ver["download_url"] == download_url, "download_url round-trips")
            check(ver["checksum"] == checksum, "checksum round-trips")

            # 5. download + verify checksum (mirrors installer.0 verifySha256)
            print("[5] download tarball + verify sha256 (installer flow)")
            blob = httpx.get(ver["download_url"], timeout=10).content
            got = "sha256:" + hashlib.sha256(blob).hexdigest()
            check(got == ver["checksum"], "downloaded bytes match published checksum")

            # extract to prove it is the package we packed (build input)
            extract_dir = tmp / "installed"
            extract_dir.mkdir()
            with tarfile.open(fileobj=__import__("io").BytesIO(blob), mode="r:gz") as t:
                t.extractall(extract_dir)
            installed_manifest = extract_dir / "zero-http-1.0.0" / "zero.json"
            check(installed_manifest.exists(), "installed package contains zero.json")

            # build the installed package with the real zero compiler if present
            installed_pkg = extract_dir / "zero-http-1.0.0"
            zero_bin = HERE.parent / "bin" / "zero"
            native = HERE.parent / ".zero" / "bin" / "zero"
            if zero_bin.exists() and native.exists():
                import subprocess

                proc = subprocess.run(
                    [str(zero_bin), "check", str(installed_pkg)],
                    capture_output=True, text=True, cwd=str(HERE.parent),
                )
                check(proc.returncode == 0,
                      f"`zero check` on installed package succeeds: {proc.stdout.strip()}{proc.stderr.strip()}")
            else:
                print("  skip: zero compiler not built — skipping build of installed package")

            # 6. search
            print("[6] GET /v1/packages/search?q=http")
            r = client.get("/v1/packages/search", params={"q": "http"})
            check(r.status_code == 200, "search 200")
            res = r.json()
            check(res["total"] == 1 and res["results"][0]["name"] == "zero-http", "search finds package")

            # 7. publish a newer version, latest must advance
            print("[7] publish zero-http@1.2.0, latest advances")
            client.post("/v1/packages", json={
                "name": "zero-http", "version": "1.2.0",
                "download_url": download_url, "checksum": checksum,
                "description": "Minimal HTTP helpers for zerolang", "token": token,
            })
            info2 = client.get("/v1/packages/zero-http").json()
            check(info2["latest"] == "1.2.0", "latest is 1.2.0 after second publish")

            # 8. delete a version
            print("[8] DELETE /v1/packages/zero-http/1.2.0")
            r = client.delete("/v1/packages/zero-http/1.2.0", params={"token": token})
            check(r.status_code == 200, "delete 200")
            check(client.get("/v1/packages/zero-http/1.2.0").status_code == 404, "deleted version is 404")
            check(client.get("/v1/packages/zero-http").json()["latest"] == "1.0.0", "latest falls back to 1.0.0")

            client.close()

    print("\nALL INTEGRATION CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
