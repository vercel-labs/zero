## Install The Compiler

Install Zerolang when you want an agent to build graph-first programs on your
machine. The compiler is experimental. Use it in isolated workspaces and avoid
production data.

## Install The Latest Release

```sh
curl -fsSL https://zerolang.ai/install.sh | bash
export PATH="$HOME/.zero/bin:$PATH"
zero --version
```

The installer downloads the latest release asset for your platform and checks
the release checksum file before installing the binary.

## Verify The Environment

```sh
zero doctor
zero targets
zero skills
```

`zero doctor --json` includes host and toolchain readiness. `zero targets --json`
includes `targetToolchains`, target aliases, hosted capability facts, and
cross-target support notes.

## Load Version-Matched Agent Knowledge

Agents should not rely on a stale external Zero guide. Ask the installed
compiler for the skills bundled with that exact binary:

```sh
zero skills
zero skills get agent
zero skills get graph
zero skills get language
zero skills get stdlib
```

The thin external Zero skill is only a bootstrap stub. The compiler-bundled
skills are the current command and language reference for that release.

## Repository Checkout

When working inside the Zero compiler repository, build the local compiler and
then use the checkout's `zero` binary for experiments:

```sh
pnpm install
make -C native/zero-c
bin/zero --version
```

The repository contributor notes cover checkout-specific wrapper commands.
