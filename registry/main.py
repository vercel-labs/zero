"""
zero-pm registry server
=======================

Human-friendly:  GET  /              → HTML index page
                 GET  /ui            → package browser (HTML)
Agent-friendly:  All /v1/* endpoints → JSON REST API

Run:
    uvicorn main:app --host 0.0.0.0 --port 8000 --reload

Environment variables:
    REGISTRY_DATA_FILE   path for JSON storage (default: data/registry.json)
    REGISTRY_TOKEN       publish auth token    (default: "" = disabled)
    REGISTRY_TITLE       page title / branding (default: zero-pm registry)
"""

from __future__ import annotations

import os
from typing import Optional

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware

from models import (
    PackageInfoResponse,
    VersionInfoResponse,
    PublishRequest,
    SearchResponse,
    SearchResult,
    OkResponse,
    ErrorResponse,
)
from storage import JsonFileStorage

# ── Configuration ─────────────────────────────────────────────────────────────

DATA_FILE      = os.getenv("REGISTRY_DATA_FILE", "data/registry.json")
AUTH_TOKEN     = os.getenv("REGISTRY_TOKEN", "")
REGISTRY_TITLE = os.getenv("REGISTRY_TITLE", "zero-pm registry")

store = JsonFileStorage(DATA_FILE)

app = FastAPI(
    title=REGISTRY_TITLE,
    description="Package registry for the zerolang ecosystem.",
    version="0.1.0",
    docs_url="/v1/docs",
    redoc_url="/v1/redoc",
    openapi_url="/v1/openapi.json",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


# ── Auth helper ───────────────────────────────────────────────────────────────

def _check_token(token: str) -> None:
    if AUTH_TOKEN and token != AUTH_TOKEN:
        raise HTTPException(status_code=401, detail="invalid token")


# ── HTML helpers ──────────────────────────────────────────────────────────────

def _html_page(title: str, body: str) -> str:
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <style>
    body  {{ font-family: system-ui, sans-serif; max-width: 860px;
             margin: 0 auto; padding: 1.5rem; color: #1a1a1a; }}
    h1   {{ color: #0050c8; }}
    a    {{ color: #0050c8; text-decoration: none; }}
    a:hover {{ text-decoration: underline; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ text-align: left; padding: .4rem .7rem; border-bottom: 1px solid #ddd; }}
    th {{ background: #f0f4ff; }}
    code {{ background: #f5f5f5; padding: .1em .3em; border-radius: 3px; }}
    .badge {{ display: inline-block; background: #e0ecff; color: #003a99;
              border-radius: 3px; padding: .1em .5em; font-size: .85em; }}
  </style>
</head>
<body>{body}</body>
</html>"""


# ── Root / index ──────────────────────────────────────────────────────────────

@app.get("/", response_class=HTMLResponse, include_in_schema=False)
async def index() -> str:
    pkgs = store.list_packages()
    rows = ""
    for p in sorted(pkgs, key=lambda x: x.name):
        rows += (
            f'<tr><td><a href="/ui/packages/{p.name}">{p.name}</a></td>'
            f'<td><span class="badge">{p.latest}</span></td>'
            f'<td>{p.description}</td></tr>\n'
        )
    body = f"""
      <h1>📦 {REGISTRY_TITLE}</h1>
      <p>
        <strong>{len(pkgs)}</strong> packages available &nbsp;|&nbsp;
        <a href="/v1/docs">API docs</a> &nbsp;|&nbsp;
        <a href="/ui">Browse</a>
      </p>
      <form method="get" action="/ui/search">
        <input name="q" placeholder="search packages…" style="width:260px;padding:.3em">
        <button type="submit">Search</button>
      </form>
      <h2>All packages</h2>
      <table>
        <tr><th>Name</th><th>Latest</th><th>Description</th></tr>
        {rows or '<tr><td colspan="3"><em>no packages yet</em></td></tr>'}
      </table>
      <hr>
      <p style="font-size:.85em">
        Install with <code>0pm add &lt;name&gt;</code> &nbsp;|&nbsp;
        API: <code>GET /v1/packages/&lt;name&gt;</code>
      </p>"""
    return _html_page(REGISTRY_TITLE, body)


# ── Human UI ──────────────────────────────────────────────────────────────────

@app.get("/ui", response_class=HTMLResponse, include_in_schema=False)
async def ui_index() -> str:
    return index.__wrapped__(None) if hasattr(index, "__wrapped__") else await index()


@app.get("/ui/packages/{name}", response_class=HTMLResponse, include_in_schema=False)
async def ui_package(name: str) -> str:
    pkg = store.get_package(name)
    if pkg is None:
        raise HTTPException(status_code=404)
    rows = ""
    for v in reversed(pkg.versions):
        vinfo = store.get_version(name, v)
        url   = vinfo.download_url if vinfo else ""
        rows += (
            f'<tr><td><a href="/ui/packages/{name}/{v}">{v}</a></td>'
            f'<td><a href="{url}">{url}</a></td></tr>\n'
        )
    body = f"""
      <p><a href="/">← back</a></p>
      <h1>{name}</h1>
      <p>{pkg.description}</p>
      <p>Latest: <span class="badge">{pkg.latest}</span></p>
      <h2>Versions</h2>
      <table>
        <tr><th>Version</th><th>Download URL</th></tr>
        {rows or '<tr><td colspan="2"><em>none</em></td></tr>'}
      </table>
      <h2>Install</h2>
      <code>0pm add {name}</code>"""
    return _html_page(f"{name} — {REGISTRY_TITLE}", body)


@app.get("/ui/packages/{name}/{version}", response_class=HTMLResponse, include_in_schema=False)
async def ui_version(name: str, version: str) -> str:
    ver = store.get_version(name, version)
    if ver is None:
        raise HTTPException(status_code=404)
    deps_html = ""
    for d in ver.deps:
        deps_html += f'<li><a href="/ui/packages/{d.name}">{d.name}</a> <span class="badge">{d.version}</span></li>\n'
    body = f"""
      <p><a href="/ui/packages/{name}">← {name}</a></p>
      <h1>{name} <span class="badge">{version}</span></h1>
      <p>{ver.description}</p>
      <table>
        <tr><th>Field</th><th>Value</th></tr>
        <tr><td>download_url</td><td><a href="{ver.download_url}">{ver.download_url}</a></td></tr>
        <tr><td>checksum</td><td><code>{ver.checksum}</code></td></tr>
      </table>
      <h2>Dependencies</h2>
      {'<ul>' + deps_html + '</ul>' if deps_html else '<p><em>none</em></p>'}
      <h2>Install</h2>
      <code>0pm add {name}@{version}</code>"""
    return _html_page(f"{name}@{version} — {REGISTRY_TITLE}", body)


@app.get("/ui/search", response_class=HTMLResponse, include_in_schema=False)
async def ui_search(q: str = "") -> str:
    results = store.search(q) if q else []
    rows = ""
    for p in results:
        rows += (
            f'<tr><td><a href="/ui/packages/{p.name}">{p.name}</a></td>'
            f'<td><span class="badge">{p.latest}</span></td>'
            f'<td>{p.description}</td></tr>\n'
        )
    body = f"""
      <p><a href="/">← home</a></p>
      <h1>Search: <em>{q}</em></h1>
      <form method="get" action="/ui/search">
        <input name="q" value="{q}" style="width:260px;padding:.3em">
        <button type="submit">Search</button>
      </form>
      <table>
        <tr><th>Name</th><th>Latest</th><th>Description</th></tr>
        {rows or '<tr><td colspan="3"><em>no results</em></td></tr>'}
      </table>"""
    return _html_page(f'Search "{q}" — {REGISTRY_TITLE}', body)


# ── Agent-friendly JSON API (/v1) ─────────────────────────────────────────────

@app.get(
    "/v1/packages",
    response_model=list[PackageInfoResponse],
    summary="List all packages",
    tags=["packages"],
)
async def api_list_packages() -> list[PackageInfoResponse]:
    return [
        PackageInfoResponse(name=p.name, description=p.description, latest=p.latest)
        for p in store.list_packages()
    ]


@app.get(
    "/v1/packages/search",
    response_model=SearchResponse,
    summary="Search packages by name or description",
    tags=["packages"],
)
async def api_search(q: str = Query("", description="search term")) -> SearchResponse:
    results = store.search(q)
    return SearchResponse(
        results=[SearchResult(name=p.name, description=p.description, latest=p.latest) for p in results],
        total=len(results),
    )


@app.get(
    "/v1/packages/{name}",
    response_model=PackageInfoResponse,
    summary="Get package metadata",
    tags=["packages"],
    responses={404: {"model": ErrorResponse}},
)
async def api_get_package(name: str) -> PackageInfoResponse:
    pkg = store.get_package(name)
    if pkg is None:
        raise HTTPException(status_code=404, detail=f"package '{name}' not found")
    return PackageInfoResponse(name=pkg.name, description=pkg.description, latest=pkg.latest)


@app.get(
    "/v1/packages/{name}/{version}",
    response_model=VersionInfoResponse,
    summary="Get version metadata",
    tags=["packages"],
    responses={404: {"model": ErrorResponse}},
)
async def api_get_version(name: str, version: str) -> VersionInfoResponse:
    ver = store.get_version(name, version)
    if ver is None:
        raise HTTPException(status_code=404, detail=f"version '{name}@{version}' not found")
    return VersionInfoResponse(
        name=ver.name,
        version=ver.version,
        download_url=ver.download_url,
        checksum=ver.checksum,
        description=ver.description,
        deps=ver.deps,
    )


@app.post(
    "/v1/packages",
    response_model=OkResponse,
    status_code=201,
    summary="Publish a new package version",
    tags=["publish"],
    responses={401: {"model": ErrorResponse}, 422: {"model": ErrorResponse}},
)
async def api_publish(req: PublishRequest) -> OkResponse:
    _check_token(req.token)
    if not req.name:
        raise HTTPException(status_code=422, detail="name is required")
    if not req.version:
        raise HTTPException(status_code=422, detail="version is required")
    if not req.download_url:
        raise HTTPException(status_code=422, detail="download_url is required")

    from models import PackageVersion, DepSpec
    pkg = PackageVersion(
        name=req.name,
        version=req.version,
        download_url=req.download_url,
        checksum=req.checksum,
        description=req.description,
        deps=req.deps,
    )
    store.put_version(pkg)
    return OkResponse(message=f"published {req.name}@{req.version}")


@app.delete(
    "/v1/packages/{name}/{version}",
    response_model=OkResponse,
    summary="Delete a package version",
    tags=["publish"],
    responses={401: {"model": ErrorResponse}, 404: {"model": ErrorResponse}},
)
async def api_delete_version(
    name: str,
    version: str,
    token: str = Query("", description="auth token"),
) -> OkResponse:
    _check_token(token)
    if not store.delete_version(name, version):
        raise HTTPException(status_code=404, detail=f"version '{name}@{version}' not found")
    return OkResponse(message=f"deleted {name}@{version}")


# ── Health ────────────────────────────────────────────────────────────────────

@app.get("/v1/health", include_in_schema=False)
async def health() -> dict:
    return {"status": "ok"}
