"""
In-process storage backend for the zero-pm registry.

Production deployments can swap this for a database-backed implementation by
implementing the same Storage ABC.  The default JsonFileStorage persists data
as a single JSON file so the registry survives restarts with no extra infra.
"""

from __future__ import annotations

import json
import os
import threading
from abc import ABC, abstractmethod
from pathlib import Path
from typing import Optional

from models import PackageInfo, PackageVersion, DepSpec


def _version_key(v: str) -> tuple[int, ...]:
    """Convert 'X.Y.Z' to a tuple for comparison; non-semver sorts last."""
    try:
        return tuple(int(x) for x in v.split("."))
    except ValueError:
        return (0, 0, 0)


class Storage(ABC):
    @abstractmethod
    def list_packages(self) -> list[PackageInfo]: ...

    @abstractmethod
    def get_package(self, name: str) -> Optional[PackageInfo]: ...

    @abstractmethod
    def get_version(self, name: str, version: str) -> Optional[PackageVersion]: ...

    @abstractmethod
    def put_version(self, pkg: PackageVersion) -> None: ...

    @abstractmethod
    def delete_version(self, name: str, version: str) -> bool: ...

    @abstractmethod
    def search(self, query: str) -> list[PackageInfo]: ...


class JsonFileStorage(Storage):
    """
    Thread-safe, file-backed JSON storage.

    Schema on disk:
    {
        "<pkg-name>": {
            "description": "...",
            "versions": {
                "<version>": {PackageVersion fields}
            }
        },
        ...
    }
    """

    def __init__(self, path: str | Path = "data/registry.json") -> None:
        self._path = Path(path)
        self._lock = threading.RLock()
        self._path.parent.mkdir(parents=True, exist_ok=True)
        if not self._path.exists():
            self._path.write_text("{}", encoding="utf-8")

    # ── internal helpers ──────────────────────────────────────────────────────

    def _load(self) -> dict:
        try:
            return json.loads(self._path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            return {}

    def _save(self, data: dict) -> None:
        tmp = self._path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
        tmp.replace(self._path)

    def _latest_version(self, versions: dict) -> str:
        if not versions:
            return ""
        return max(versions.keys(), key=_version_key)

    # ── Storage interface ─────────────────────────────────────────────────────

    def list_packages(self) -> list[PackageInfo]:
        with self._lock:
            data = self._load()
            result: list[PackageInfo] = []
            for name, pkg in data.items():
                versions = pkg.get("versions", {})
                result.append(PackageInfo(
                    name=name,
                    description=pkg.get("description", ""),
                    latest=self._latest_version(versions),
                    versions=sorted(versions.keys(), key=_version_key),
                ))
            return result

    def get_package(self, name: str) -> Optional[PackageInfo]:
        with self._lock:
            data = self._load()
            pkg = data.get(name)
            if pkg is None:
                return None
            versions = pkg.get("versions", {})
            return PackageInfo(
                name=name,
                description=pkg.get("description", ""),
                latest=self._latest_version(versions),
                versions=sorted(versions.keys(), key=_version_key),
            )

    def get_version(self, name: str, version: str) -> Optional[PackageVersion]:
        with self._lock:
            data = self._load()
            pkg = data.get(name)
            if pkg is None:
                return None
            ver = pkg.get("versions", {}).get(version)
            if ver is None:
                return None
            return PackageVersion(
                name=name,
                version=version,
                download_url=ver.get("download_url", ""),
                checksum=ver.get("checksum", ""),
                description=ver.get("description", pkg.get("description", "")),
                deps=[DepSpec(**d) for d in ver.get("deps", [])],
            )

    def put_version(self, pkg: PackageVersion) -> None:
        with self._lock:
            data = self._load()
            entry = data.setdefault(pkg.name, {"description": pkg.description, "versions": {}})
            # Update top-level description if the new version includes one
            if pkg.description:
                entry["description"] = pkg.description
            entry["versions"][pkg.version] = {
                "download_url": pkg.download_url,
                "checksum": pkg.checksum,
                "description": pkg.description,
                "deps": [d.model_dump() for d in pkg.deps],
            }
            self._save(data)

    def delete_version(self, name: str, version: str) -> bool:
        with self._lock:
            data = self._load()
            pkg = data.get(name)
            if pkg is None:
                return False
            versions = pkg.get("versions", {})
            if version not in versions:
                return False
            del versions[version]
            if not versions:
                del data[name]
            self._save(data)
            return True

    def search(self, query: str) -> list[PackageInfo]:
        q = query.lower()
        return [
            p for p in self.list_packages()
            if q in p.name.lower() or q in p.description.lower()
        ]
