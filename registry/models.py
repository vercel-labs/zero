from __future__ import annotations

from typing import Optional
from pydantic import BaseModel, Field


class DepSpec(BaseModel):
    name: str
    version: str


class PackageVersion(BaseModel):
    name: str
    version: str
    download_url: str
    checksum: str = ""          # "sha256:<hex>" or "crc32:<hex>"
    description: str = ""
    deps: list[DepSpec] = Field(default_factory=list)


class PackageInfo(BaseModel):
    name: str
    description: str = ""
    latest: str = ""
    versions: list[str] = Field(default_factory=list)


# ── Requests ──────────────────────────────────────────────────────────────────

class PublishRequest(BaseModel):
    name: str
    version: str
    download_url: str
    checksum: str = ""
    description: str = ""
    deps: list[DepSpec] = Field(default_factory=list)
    token: str = ""             # auth token (checked server-side)


# ── Responses ─────────────────────────────────────────────────────────────────

class PackageInfoResponse(BaseModel):
    name: str
    description: str
    latest: str


class VersionInfoResponse(BaseModel):
    name: str
    version: str
    download_url: str
    checksum: str
    description: str
    deps: list[DepSpec]


class SearchResult(BaseModel):
    name: str
    description: str
    latest: str


class SearchResponse(BaseModel):
    results: list[SearchResult]
    total: int


class ErrorResponse(BaseModel):
    error: str


class OkResponse(BaseModel):
    ok: bool = True
    message: str = ""
