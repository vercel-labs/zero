# zero-pm registry

Package registry for the zerolang ecosystem. Serves a human-friendly HTML browser
at `/` and `/ui`, and an agent-friendly JSON REST API under `/v1`.

The `0pm` client (`zero-pm/src/registry.0`, `installer.0`) talks to this server:

| Client call              | Endpoint                          | Returns                         |
|--------------------------|-----------------------------------|---------------------------------|
| `fetchPackageInfo`       | `GET /v1/packages/<name>`         | `{name, description, latest}`   |
| `fetchVersionInfo`       | `GET /v1/packages/<name>/<ver>`   | `{name, version, download_url, checksum, ...}` |
| publish                  | `POST /v1/packages`               | `{ok, message}`                 |

## Run

```bash
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

Environment variables:

- `REGISTRY_DATA_FILE` — JSON storage path (default `data/registry.json`)
- `REGISTRY_TOKEN` — publish auth token (default empty = publishing open)
- `REGISTRY_TITLE` — page title / branding

## Integration test

Drives a live server through the full flow the `0pm` client uses — pack a
package, publish it, read package/version metadata, download and verify the
sha256 checksum, then `zero check` the installed package — plus search,
`latest` tracking, and delete:

```bash
python3 test_integration.py
```

Exit code `0` means every assertion passed. The build step is skipped
automatically if the native `zero` compiler (`.zero/bin/zero`) is not built.
