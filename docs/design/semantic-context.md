# Semantic Context Projection V0

This note defines an exploratory v0 schema for source-anchored semantic
context projection in Zero. It is a small design artifact, not a compiler
command or storage implementation.

Zero already emits structured development-time facts through diagnostics,
`zero explain --json`, repair metadata, `zero fix --plan --json`, graph JSON,
command-contract snapshots, and conformance tests. A semantic context
projection builds on those surfaces by preserving a narrow piece of repair
rationale together with the source span and machine-readable repair identity
that produced it.

The first use case is repair memory: a reviewable JSON artifact that records
why a known repair exists and where it applies. For example, the existing
`TYP009` / `make-binding-mutable` repair can be represented as a source-bound
context node that points at the `let` token, records the diagnostic and repair
IDs, and carries a concise residual summary for future inspection.

## V0 Scope

The v0 schema is intentionally narrow:

- Node kind is limited to `repair-memory`.
- Source anchors include `path`, `range`, `columnUnit`, `sourceHash`, and
  `status`.
- Semantic codes are strings, not semantic bytecode.
- Natural-language context is stored as `residualSummary`, not as full
  residual storage.
- `parents` is present for lineage shape, but there is no DAG storage.
- `sourceHash` may be `null` while source invalidation is not implemented.
- `hash` is omitted until Merkle canonicalization rules exist.
- Projection output is a small active frontier: diagnostics, repairs, and
  reviewable edit facts.

The fixture in `conformance/context/repair-memory-typ009.projection.json`
shows the complete v0 shape.

## Deferred Work

This schema does not implement:

- learned codecs;
- adaptive codebooks;
- compiler reconciliation;
- context diagnostics;
- `.zero/context` sidecar storage;
- Merkle canonicalization;
- source invalidation;
- compressed residual packs;
- a `zero context` command.

These are larger design spaces that should remain separate from the v0 schema
until the repository has reviewed a concrete projection shape.

## Relation To Existing Zero Surfaces

The v0 projection is grounded in existing compiler output:

- `diagnosticCode` matches the stable diagnostic code from `zero check --json`
  and `zero fix --plan --json`.
- `repairId` matches existing repair metadata and explain output.
- `sourceAnchor.range` follows the same line, column, and `utf8-byte` column
  unit convention used by graph and fix-plan JSON.
- `projection.frontier.edits` mirrors the agent-safe edit preview emitted by
  `zero fix --plan --json` for previewable repairs.
- `residualSummary` provides a compact explanation that can later be derived
  from or linked to `zero explain --json`.

This keeps the schema reviewable without requiring new compiler authority.

## Possible Evolution

If the v0 shape proves useful, it can evolve in small steps:

1. `zero context project --json` could emit projection packets by joining
   existing diagnostics, explain metadata, graph ranges, and fix-plan previews.
2. `.zero/context` sidecar storage could persist reviewed projection nodes.
3. Context verification could check anchor status, source hashes, parent
   references, and schema compatibility.
4. Semantic reconciliation could compare edited source against active context
   nodes and ask for an explicit context update when repair rationale or source
   anchors drift.

Each step should preserve the current Zero principle: structured facts first,
compiler-enforced behavior only after the contract is narrow and testable.
