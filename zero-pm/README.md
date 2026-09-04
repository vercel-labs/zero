# zero-pm (`0pm`)

Package manager CLI for the zerolang ecosystem, written in Zero. Talks to the
registry server in `../registry` and installs packages into
`~/.zero/packages/<name>/<version>/`.

## Build

```sh
make -C native/zero-c                 # the zero compiler (once, from repo root)
make -C zero-pm/native                # native helpers (sha256, mkdir -p, tar+gzip)
bin/zero build --emit exe zero-pm --out .zero/out/0pm
```

Host requirements: libcurl (HTTP runtime) and zlib (`-lz`, tar.gz extraction).

## Use

```sh
export ZERO_REGISTRY=http://127.0.0.1:8000   # default: https://registry.zerolang.ai

0pm init my-app          # create zero.json
0pm add zero-http@1.0.0  # pin a version
0pm add zero-json        # resolve latest from the registry
0pm list                 # read .zero/zero.lock.json
0pm info zero-http       # lockfile first, registry fallback
0pm update [name]        # bump pinned versions to latest, reinstall
0pm remove zero-http     # drop from manifest + lockfile
0pm install              # install everything in zero.json
```

Downloads are checksum-verified (`sha256:<hex>` or `crc32:<hex>`); `.tar.gz`
and raw ustar archives are extracted into the cache, anything else is stored
as `<name>.0`. A failed `add` rolls `zero.json` back.

## Test

End-to-end (builds `0pm`, boots the registry + a tarball server, drives every
command, asserts manifests/lockfiles/cache/rollback):

```sh
pip install -r registry/requirements.txt
python3 zero-pm/test_e2e.py
```

Registry-only protocol test: `python3 registry/test_integration.py`.

## Design notes (direct-backend subset)

The zero direct backend currently lowers a restricted subset, which shapes the
code style in `src/`:

- no shape/choice/enum cross-function values — state lives in `[N]u8` buffers
  with explicit lengths; modules expose buffer-in/buffer-out functions
- `World` is only usable in `main`, so commands append output into a caller
  buffer that `main` writes once
- calls are limited to eight ABI argument slots (a span costs two), so fetch
  and install helpers own their HTTP clients and scratch buffers
- `extern c` requires `c.libs` link metadata in `zero.json` (static archive +
  `link: ["z"]`)
- `zero test` direct-runner only lowers scalar test bodies, so behavior is
  covered by `test_e2e.py` instead of in-file `test` blocks
