---
name: testing
description: Write and run Zero test blocks.
---

# Zero Testing

Use this when adding tests, debugging failing tests, or wiring Zero checks into CI and editor workflows.

## Test Blocks

Zero test blocks live beside source:

```zero
fn add(left: i32, right: i32) -> i32 {
    return left + right
}

test "addition works" {
    expect add(2, 3) == 5
}
```

`expect` requires a `Bool`. A false expectation fails the test.

Use `std.testing` helpers when a predicate should be explicit in the source:

```zero
test "output shape" {
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
}
```

`std.testing` does not register tests or print output. It only returns `Bool`
values for ordinary `expect` statements.

## Run

```sh
zero test conformance/native/pass/test-blocks.graph
zero test --filter addition conformance/native/pass/test-blocks.graph
zero test conformance/packages/test-app
```

Use `--filter` for a narrow loop. The filter matches test names by substring.

For packages, normal `zero test [package]` compiles from `zero.graph` and can
run before `.0` projections exist:

```sh
zero patch --op 'addTest name="addition works" call="add" arg0="2" arg1="3" expect="5" type="i32"'
zero test
zero test --filter addition
```

Prefer `addTest` for one pure function call with literal arguments. Use
`addTestBody name="..." ... end` only when the test needs custom body rows.
Test labels are display names, not callable function names; do not rename them
to `__zero_test_*`.
Test failures name the callee the runner could not execute. A `does not
implement 'std.x.y' yet` failure means the function is real but the test
runner only executes `std.testing` helpers and pure zerolang functions; keep
that logic covered with a live smoke check (`zero run . -- <args>`) instead of
deleting the test target. An `unknown function '<name>'` failure on a non-std
name means the call does not resolve in the test graph; fix the call or
re-import the package.

If another tool hands you a derived ProgramGraph artifact, `zero test` can
validate it. Do not create a standalone graph artifact for the ordinary package
test loop; test the package path so the compiler reads `zero.graph` directly.

## JSON Fields

Use `zero test --json` when a tool or CI job needs exact fields. Useful fields:

- `testDiscovery`: how files and tests were found
- `fixtures`: fixture inputs and snapshot metadata
- `snapshotKey`: stable test snapshot contract
- `discoveredTests`, `selectedTests`, `passedTests`, `failedTests`
- `expectedFailures`, `unexpectedPasses`
- `targetFacts`: selected target and capability facts
- `results`: per-test name, status, duration, source location, and failure span
- `stdout`, `stderr`, `durationMs`

Use JSON for machines and CI contracts. Normal test output is the default agent loop.

## Expected Failures

Expected-fail tests use one of these name markers:

- `xfail:`
- `expected fail:`
- `[xfail]`

Example:

```zero
test "xfail: pending parser edge case" {
    expect false
}
```

An expected-fail test passes the command only when it fails as expected. If it starts passing, the command fails with `unexpectedPasses`.

## Agent Workflow

1. Add the smallest test that owns the behavior.
2. Run a filtered test while editing.
3. Run the containing package or fixture before finishing.
4. Do not leave an expected-fail marker on a fixed bug.
5. Use `zero check` first when the failure is a compile error; rerun with `--json` only if you need exact diagnostic fields.
