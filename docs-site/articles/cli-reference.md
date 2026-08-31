## CLI Reference

`zero` is the product shell for checking, formatting, building, testing, inspecting, and repairing Zero projects.

Core commands:

```sh
zero --version [--json]
zero skills [list|get|path] [--json]
zero new cli|lib|package <path>
zero doctor [--json]
zero check [--json] [--target <target>] <input>
zero dev [--json] [--trace] [--target <target>] <input>
zero build [--emit exe|obj|wasm] [--target <target>] [--profile dev|release] [--out <file>] <input>
zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>
zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <input>
zero fmt [--check | --write] [--json] <input>
zero graph [--json] [--target <target>] <input>
zero doc [--json] [--target <target>] <input>
zero size [--json] [--target <target>] [--out <artifact>] <input>
zero explain [--json] <diagnostic-code>
zero fix --plan --json [--target <target>] <input>
zero targets
zero clean [--all]
```

`<input>` may be a `.0` source file, a package directory, or a `zero.json` manifest. JSON modes are stable enough for agents and tests. `zero check --json` and `zero graph --json` include `compileTime` for deterministic bounded `meta` evaluation, sandbox denials, cache key inputs, typed reflection facts, integer/Bool/enum static values, and limited typed builder metadata. `zero dev --json --trace` emits the current watch plan, source/manifest/package-lock/generated-binding inputs, affected checks/tests/examples, restart behavior for runnable CLI targets, interface fingerprints, cache hit/miss facts, phase timing, and diagnostics passthrough metadata. `zero time --json` reports the same `interfaceFingerprints` and `incrementalInvalidation` cache facts for editor and CI timing audits. `zero build --json` includes a `toolchain` object with the selected compiler, selection source, target triple, linker flavor, and sysroot status. Build and ship JSON also expose `releaseTargetContract`, which records the artifact kind, object format, direct linker flavor, target libc mode, sysroot requirement/status, emitter readiness, target capability facts, and repeat-build hash policy. `zero ship --json` produces a release preview with the binary, stripped binary copy, checksum file, deterministic archive manifest, debug-symbol metadata, size report, SBOM placeholder, artifact names, sizes, hashes, target facts, and the same `releaseTargetContract` nested under `releasePreview.targetContract`. Use `--emit wasm` for WebAssembly artifacts and `--emit obj` for native objects. Removed backend flags report `BLD003`. `zero doctor --json` includes checks for the host target, PATH health, target SDK/sysroot readiness, workspace writes, bundled targets, docs, examples, and a `targetToolchains` array with per-target readiness facts. `zero fix` is plan-only and does not apply edits.

`zero fmt --check` exits non-zero when the input would be reformatted, matching the shape of `gofmt -l` and `rustfmt --check`. `zero fmt --write` overwrites the source file in place. Both modes accept `--json` for a structured envelope: `{ "schemaVersion": 1, "command": "fmt", "mode": "check"|"write", "sourceFile": path, "ok": bool, "unformatted"|"written": [paths] }`. `--check` and `--write` are mutually exclusive; bare `zero fmt` continues to print formatted output to stdout.

`zero skills` serves bundled skill content for agents. Use `zero skills list` to discover available skills, `zero skills get zero` to print the current Zero workflow, `zero skills get zero --full` to include references and templates, and `zero skills path zero` to print the local skill directory. `--json` returns `{ "success": true, "data": ... }` payloads for automation. Set `ZERO_SKILLS_DIR` to point the command at an alternate skill directory.

`zero test --json` reports package and fixture discovery for CI and editor integrations. The payload includes `testDiscovery`, `fixtures`, `targetFacts`, `results`, `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`, `expectedFailures`, `unexpectedPasses`, `durationMs`, `stdout`, and `stderr`. `fixtures.snapshotKey` identifies the test snapshot contract, and each result includes a source location plus a failure span when available. Expected-fail tests use `xfail:`, `expected fail:`, or `[xfail]` in the test name; an unexpected pass fails the command.

Profile-aware build and size JSON include `profileSemantics`, `profileCatalog`, and `profileBudget` for `debug`, `fast`, `small`, and `tiny`. `zero size --json` also emits `sizeBreakdown`, `retentionReasons`, and `optimizationHints` so optimization agents can explain retained functions, sections, literals, stdlib helpers, imports, runtime shims, and debug metadata.

`npm run zls -- --self-test` exercises the language-server smoke path: diagnostics, hover docs with target/capability/generated-binding facts, completions, go-to definition, document symbols, and quick-fix code actions surfaced from `zero fix` for `TAR002`, `TYP009`, `ERR002`, `ERR003`, and `PUB001`.
