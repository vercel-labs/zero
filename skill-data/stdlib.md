---
name: stdlib
description: Use Zero standard library modules and target-gated capabilities.
---

# Zero Standard Library

Use this for common library calls, memory helpers, hosted I/O, or target-capability guidance.

## Import

```zero
use std.mem

use std.parse
```

Call functions with their module path, such as `std.mem.len(value)`.

## Target-Neutral Helpers

- `std.mem`: spans, byte copy/fill, non-owned scalar item copy/fill/search/compare, scalar item slicing, chunking, cursor helpers, length, safe indexed `get`, fixed-buffer allocation, byte buffers, and caller-owned vectors.
- `std.collections`: fixed-capacity push/pop, deque front/back operations, first/last access, indexed insert/replace/remove, unique insert, append, clear, truncate, fill, reverse, rotate, live-prefix and set/map views, `FixedSet<T>`, `FixedDeque<T>`, `FixedRingBuffer<T>`, and `FixedMap<K,V>` storage wrappers, set/map/ring-buffer capacity state, count, contains, value removal, swap, swap-remove, move-to-front, and parallel key/value map helpers over caller-owned array or allocator-returned span storage plus explicit lengths.
- `std.search`: generic scalar index search plus typed min/max, lower-bound, upper-bound, and binary-search helpers.
- `std.sort`: in-place insertion, stable, unstable, swap, rotate, reverse, partition, sorted dedupe, and sortedness helpers for ascending and descending `i32`, `u32`, and `usize` storage.
- `std.ascii`: ASCII byte predicates, case conversion, and digit value helpers.
- `std.fmt`: caller-buffer and fixed-writer formatting for booleans, 32-bit and 64-bit integer text, integer bases, signs, and padding.
- `std.text`: ASCII and UTF-8 byte-backed text validation.
- `std.unicode`: strict UTF-8 codepoint decode/encode iteration, cursor status helpers, and codepoint-class helpers; pair with `std.text.utf8Valid`/`std.text.utf8Len` for whole-span validation and counting.
- `std.math`: fixed-width min/max/clamp, checked and saturating integer arithmetic, GCD/LCM, powers, modular power, roots, combinatorics, primality, and divisor routines.
- `std.path`: target-neutral lexical path basename, dirname, extension, stem, component, abs, join, normalize, and relative helpers.
- `std.codec`: endian reads/writes, unsigned and signed varints, base32/base64/hex encode/decode, CRC helpers, and byte checksums.
- `std.csv`: allocation-free CSV validation, record scanning, field decoding, and fixed-arity writers.
- `std.parse`: byte scanners plus decimal, radix, prefix integer width, bool, duration, and byte-size parsers returning `Maybe<T>`.
- `std.regex`: compile-once and one-shot regular expression matching/search/split/replace for a documented ECMA-262-leaning subset (literals, classes, anchors, word boundaries, greedy quantifiers, alternation, groups); unsupported constructs fail with structured status codes and offsets.
- `std.inet`: target-neutral IPv4/IPv6/hostname literal validation and parsing; no network capability needed.
- `std.time`: duration construction, conversion, comparison, elapsed-window helpers, hosted sleep, RFC 3339 date/time validation and epoch parsing, and target-gated clock helpers.
- `std.rand`: explicit deterministic random sources, random bits, unbiased bounded/range helpers, target entropy helpers, and caller-buffer entropy IDs.
- `std.crypto`: small hashes, SHA-256 digest writers, fixed-width hash text, byte-oriented crypto helpers, and caller-buffer IDs.
- `std.json`: explicit-buffer JSON validation, structured status/location diagnostics, object/array cursor lookup, typed scalar decode, parsing, and string/object writing helpers.
- `std.toml`: no-allocation TOML validation, shallow/dotted field lookup, and typed scalar decode helpers.
- `std.url`: target-neutral URL splitting, percent/query/form encoding and decoding, query/form lookup, and query append helpers.
- `std.str`: byte-span string helpers, including non-overlapping reverse, copy/concat/repeat/replace, prefix/suffix, split, fields, lines, trim, and word counts.
- `std.io`: buffered reader/writer surfaces, cursor writes, line scanning, and byte copy over caller-owned storage.
- `std.testing`: Bool-returning helpers for test blocks and byte-output checks.
- `std.term`: ANSI terminal style, cursor, clear, alternate-screen sequence helpers, target-neutral key-byte decoding, and hosted TTY/size/raw-mode/input helpers.
- `std.log`: explicit-buffer JSON Lines record formatting.

Prefer `Maybe<T>` return checks over assuming an operation succeeded.

## Hosted Capabilities

These modules depend on host or runtime capabilities:

- `std.args`: process arguments
- `std.cli`: command-line flag, option, and typed option helpers over process arguments
- `std.env`: process environment lookup, comparisons, and typed fallback parsing
- `std.fs`: hosted filesystem, explicit `Fs` or `owned<File>` handles, and file-level byte helpers
- `std.net`: bootstrap network handles
- `std.http`: HTTP request/response helpers and loopback listeners
- `std.proc`: process execution and exit-status helpers
- `std.pty`: hosted pseudoterminal child processes
- `World.out` and `World.err`: program output capabilities

Non-host targets may reject these APIs with target diagnostics. Inspect target facts before cross-building:

```sh
zero targets
zero check --target linux-musl-x64 [graph-input]
zero inspect --target linux-musl-x64 [graph-input]
```

Add `--json` only when a tool needs exact target facts or diagnostics.

## Memory Pattern

```zero
use std.mem

pub fn main(world: World) -> Void raises {
    let bytes: Span<u8> = std.mem.span("zero")
    if std.mem.len(bytes) == 4 {
        check world.out.write("memory ok\n")
    }
}
```

For writable buffers, use caller-owned fixed arrays and `MutSpan<T>`:

```zero
pub fn main() -> Void {
    var storage: [8]u8 = [0, 0, 0, 0, 0, 0, 0, 0]
    let writable: MutSpan<u8> = storage
    let copied: usize = std.mem.copy(writable, std.mem.span("zero"))
}
```

For non-byte scalar item storage, use the generic item helpers. Current direct
targets support `Bool`, `u8`, `u16`, `usize`, `i32`, `u32`, `i64`, and `u64`
elements for these helpers.

```zero
pub fn main() -> Void {
    var values: [4]i32 = [1, 2, 3, 4]
    var scratch: [4]i32 = [0, 0, 0, 0]
    let copied: usize = std.mem.copyItems(scratch, values)
    let prefix: Span<i32> = std.mem.prefix(scratch, 2)
    let suffix: Span<i32> = std.mem.suffix(scratch, 2)
    let before: Span<i32> = std.mem.splitBefore(scratch, 3)
    let after: Span<i32> = std.mem.splitAfter(scratch, 3)
    let middle: Span<i32> = std.mem.slice(scratch, 1, 2)
    let chunk: Span<i32> = std.mem.chunk(scratch, 1_usize, 2_usize)
    let sliding: Span<i32> = std.mem.window(scratch, 1_usize, 2_usize)
    let cursor: usize = std.mem.advance(scratch, 0_usize, 2_usize)
    let rest: Span<i32> = std.mem.remaining(scratch, cursor)
    expect copied == 4 && std.mem.contains(prefix, 1) && std.mem.compareI32(prefix, suffix) < 0 && std.mem.len(suffix) == 2 && std.mem.len(before) == 2 && std.mem.len(after) == 1 && std.mem.len(middle) == 2 && std.mem.len(chunk) == 2 && std.mem.len(sliding) == 2 && std.mem.len(rest) == 2
}
```

Fixed-capacity collection helpers keep storage and length explicit:

```zero
pub fn main() -> Void {
    var values: [4]i32 = [0, 0, 0, 0]
    var len: usize = 0
    len = std.collections.push(values, len, 3)
    len = std.collections.push(values, len, 1)
    len = std.collections.setInsert(values, len, 5)
    len = std.collections.insertAt(values, len, 1_usize, 4)
    let replaced: Bool = std.collections.replaceAt(values, len, 1_usize, 9)
    let swapped: Bool = std.collections.swapAt(values, len, 0_usize, 1_usize)
    let reversed: Bool = std.collections.reverse(values, len)
    let filled: Bool = std.collections.fill(values, 2_usize, 7)
    let rotated_left: Bool = std.collections.rotateLeft(values, len, 1_usize)
    let rotated_right: Bool = std.collections.rotateRight(values, len, 1_usize)
    len = std.collections.removeAt(values, len, 2_usize)
    let last: Maybe<i32> = std.collections.last(values, len)
    len = std.collections.dequePushFront(values, len, 2)
    let front: Maybe<i32> = std.collections.dequeFront(values, len)
    len = std.collections.dequePopFront(values, len)
    let back_len: usize = std.collections.dequePopBack(values, len)
    len = std.collections.truncate(values, len, 3)
    let set_live: Span<i32> = std.collections.setView(values, len)
    let set_remaining: usize = std.collections.setRemaining(values, len)
    let live: Span<i32> = std.collections.view(values, len)
    var fixed_storage: [4]i32 = [1, 2, 0, 0]
    var fixed_set: FixedSet<i32> = std.collections.fixedSet(fixed_storage, 2_usize)
    let fixed_inserted: Bool = std.collections.fixedSetInsert(&mut fixed_set, 3)
    let fixed_removed: Bool = std.collections.fixedSetRemove(&mut fixed_set, 1)
    let fixed_live: Span<i32> = std.collections.fixedSetView(&fixed_set)
    var fixed_deque_storage: [4]i32 = [0, 0, 0, 0]
    var fixed_deque: FixedDeque<i32> = std.collections.fixedDeque(fixed_deque_storage, 0_usize)
    let fixed_deque_back_pushed: Bool = std.collections.fixedDequePushBack(&mut fixed_deque, 2)
    let fixed_deque_front_pushed: Bool = std.collections.fixedDequePushFront(&mut fixed_deque, 1)
    let fixed_deque_front: Maybe<i32> = std.collections.fixedDequeFront(&fixed_deque)
    let fixed_deque_popped: Maybe<i32> = std.collections.fixedDequePopBack(&mut fixed_deque)
    var fixed_ring_storage: [4]i32 = [0, 0, 0, 0]
    var fixed_ring: FixedRingBuffer<i32> = std.collections.fixedRingBuffer(fixed_ring_storage, 0_usize, 0_usize)
    let fixed_ring_back_pushed: Bool = std.collections.fixedRingBufferPushBack(&mut fixed_ring, 2)
    let fixed_ring_front_pushed: Bool = std.collections.fixedRingBufferPushFront(&mut fixed_ring, 1)
    let fixed_ring_front: Maybe<i32> = std.collections.fixedRingBufferFront(&fixed_ring)
    let fixed_ring_popped: Maybe<i32> = std.collections.fixedRingBufferPopBack(&mut fixed_ring)
    var fixed_keys: [3]u8 = [1_u8, 2_u8, 0_u8]
    var fixed_scores: [3]u16 = [10_u16, 20_u16, 0_u16]
    var fixed_map: FixedMap<u8, u16> = std.collections.fixedMap(fixed_keys, fixed_scores, 2_usize)
    let fixed_map_added: Bool = std.collections.fixedMapPut(&mut fixed_map, 3_u8, 30_u16)
    let fixed_map_score: Maybe<u16> = std.collections.fixedMapGet(&fixed_map, 3_u8)
    var keys: [3]u8 = [1_u8, 2_u8, 0_u8]
    var scores: [3]u16 = [10_u16, 20_u16, 0_u16]
    var map_len: usize = 2
    map_len = std.collections.mapPut(keys, scores, map_len, 3_u8, 30_u16)
    let has_score: Bool = std.collections.mapContains(keys, map_len, 3_u8)
    let score: Maybe<u16> = std.collections.mapGet(keys, scores, map_len, 3_u8)
    let live_keys: Span<u8> = std.collections.mapKeys(keys, map_len)
    let live_scores: Span<u16> = std.collections.mapValues(keys, scores, map_len)
    expect replaced && swapped && reversed && filled && rotated_left && rotated_right && std.collections.clear(values, len) == 0 && std.collections.setClear(values, len) == 0 && std.collections.pop(values, len) == 2 && last.has && last.value == 5 && front.has && front.value == 2 && back_len == 2 && std.collections.setContains(values, len, 5) && set_remaining == 1 && std.mem.len(set_live) == 3 && std.mem.len(live) == 3 && fixed_inserted && fixed_removed && std.mem.len(fixed_live) == 2 && std.collections.fixedSetRemaining(&fixed_set) == 2 && std.collections.fixedSetLen(&fixed_set) == 2 && fixed_deque_back_pushed && fixed_deque_front_pushed && fixed_deque_front.has && fixed_deque_front.value == 1 && fixed_deque_popped.has && fixed_deque_popped.value == 2 && fixed_map_added && fixed_map_score.has && fixed_map_score.value == 30_u16 && map_len == 3 && std.collections.mapRemaining(keys, scores, map_len) == 0 && std.collections.mapIsFull(keys, scores, map_len) && std.collections.mapClear(keys, scores, map_len) == 0 && has_score && score.has && score.value == 30_u16 && std.mem.len(live_keys) == 3 && std.mem.len(live_scores) == 3
}
```

For byte-oriented dynamic storage, request mutable byte spans explicitly with
`std.mem.allocBytes`. The fixed collection wrappers can use those returned spans
as backing storage:

```zero
pub fn main() -> Void {
    var key_storage: [4]u8 = [0_u8; 4]
    var value_storage: [4]u8 = [0_u8; 4]
    var key_alloc: FixedBufAlloc = std.mem.fixedBufAlloc(key_storage)
    var value_alloc: FixedBufAlloc = std.mem.fixedBufAlloc(value_storage)
    let keys_maybe: Maybe<MutSpan<u8>> = std.mem.allocBytes(key_alloc, 4_usize)
    let values_maybe: Maybe<MutSpan<u8>> = std.mem.allocBytes(value_alloc, 4_usize)
    if keys_maybe.has && values_maybe.has {
        var map: FixedMap<u8, u8> = std.collections.fixedMap(keys_maybe.value, values_maybe.value, 0_usize)
        let ok: Bool = std.collections.fixedMapPut(&mut map, 7_u8, 42_u8)
        let value: Maybe<u8> = std.collections.fixedMapGet(&map, 7_u8)
        expect ok && value.has && value.value == 42_u8
    }
}
```

Use `std.sort` and `std.search` for common scalar algorithms instead of
hand-rolling loops:

```zero
pub fn main() -> Void {
    var values: [5]i32 = [5, 1, 4, 2, 3]
    std.sort.stableI32(values)
    expect std.sort.isSortedI32(values)
    std.sort.unstableI32(values)
    expect std.sort.isSortedI32(values)
    std.sort.reverseI32(values)
    expect std.sort.isSortedDescI32(values)
    let swapped: Bool = std.sort.swapI32(values, 0_usize, 4_usize)
    expect swapped
    std.sort.rotateLeftI32(values, 2_usize)
    expect values[0] == 3
    std.sort.rotateRightI32(values, 2_usize)
    expect values[0] == 1
    std.sort.insertionDescI32(values)
    expect std.sort.isSortedDescI32(values)
    std.sort.stableDescI32(values)
    expect std.sort.isSortedDescI32(values)
    std.sort.unstableDescI32(values)
    expect std.sort.isSortedDescI32(values)
    std.sort.insertionI32(values)
    expect std.search.binaryI32(values, 4) == 3
    expect std.search.containsSortedI32(values, 4)
    expect std.search.upperBoundI32(values, 4) == 4
    expect std.search.countSortedI32(values, 4) == 1
    std.sort.insertionDescI32(values)
    expect std.search.binaryDescI32(values, 4) == 1
    expect std.search.containsSortedDescI32(values, 4)
    expect std.search.upperBoundDescI32(values, 4) == 2
    expect std.search.countSortedDescI32(values, 4) == 1
    let high_len: usize = std.sort.partitionDescI32(values, 3)
    expect high_len == 2
    let minimum: Maybe<i32> = std.search.minI32(values)
    expect minimum.has && minimum.value == 1
    expect std.search.maxIndexI32(values) == 0
}
```

String helpers are byte-oriented and allocation-free. `std.str.reverse` writes
into caller storage and requires that destination storage does not overlap the
input text:

```zero
pub fn main() -> Void {
    var reversed: [4]u8 = [0, 0, 0, 0]
    let out: Maybe<Span<u8>> = std.str.reverse(reversed, "zero")
    if out.has {
        expect std.mem.eql(out.value, "orez")
    }
}
```

Use `std.parse` and `std.fmt` instead of hand-rolled decimal loops in ordinary
CLIs and examples:

```zero
pub fn main() -> Void {
    let parsed: Maybe<i32> = std.parse.parseI32("-42")
    var out: [12]u8 = [0_u8; 12]
    if parsed.has {
        let formatted: Maybe<Span<u8>> = std.fmt.i32(out, parsed.value)
        expect formatted.has && std.mem.eql(formatted.value, "-42")
    }
}
```

Use codec, JSON, and URL helpers for common wire-format work instead of
hand-rolled loops:

```zero
pub fn main() -> Void {
    var decoded: [4]u8 = [0_u8; 4]
    let text: Maybe<Span<u8>> = std.codec.base64Decode(decoded, "emVybw==")

    let input: Span<u8> = "{\"count\":42,\"ok\":true}"
    let count: Maybe<u32> = std.json.u32(input, "count")

    var url_buf: [48]u8 = [0_u8; 48]
    var param_buf: [16]u8 = [0_u8; 16]
    let param: Maybe<Span<u8>> = std.url.writeQueryParam(param_buf, "q", "zero lang")
    var url: Maybe<Span<u8>> = null
    if param.has {
        url = std.url.appendQuery(url_buf, "https://example.com/path", param.value)
    }

    expect text.has && count.has && url.has
}
```

Use `std.math` checked helpers when overflow is a normal input outcome:

```zero
pub fn main() -> Void {
    let value: Maybe<u32> = std.math.checkedMulU32(6_u32, 7_u32)
    if value.has {
        expect value.value == 42_u32
    }
    expect std.math.sqrtFloorU32(99) == 9
}
```

Keep random sources explicit and durations typed:

```zero
pub fn main() -> Void {
    var rng: RandSource = std.rand.seed(7_u32)
    let first: u32 = std.rand.nextU32(&mut rng)
    let bit: Bool = std.rand.nextBool(&mut rng)
    let bounded: Maybe<u32> = std.rand.nextBelow(&mut rng, 10_u32)
    let ranged: Maybe<u32> = std.rand.rangeU32(&mut rng, 40_u32, 50_u32)
    let delay: Duration = std.time.add(std.time.ms(250), std.time.seconds(1))
    expect first == 1025555898_u32 && bit && bounded.has && ranged.has && std.time.asMsFloor(delay) == 1250
}
```

Use `std.testing` inside `expect` when the comparison shape matters to readers
or agents:

```zero
test "output shape" {
    expect std.testing.equalBytes("zero", "zero")
    expect std.testing.containsBytes("zerolang", "lang")
    expect std.testing.jsonPathEquals("{\"service\":{\"ok\":true}}", "service.ok", "true")
}
```

Use `std.log` as a caller-buffer formatter, then write the resulting span
through an explicit output capability:

```zero
pub fn main(world: World) -> Void raises {
    var storage: [128]u8 = [0_u8; 128]
    var field_storage: [64]u8 = [0_u8; 64]
    let field: Maybe<Span<u8>> = std.log.stringField(field_storage, "event", "startup")
    if field.has {
        let entry: Maybe<Span<u8>> = std.log.messageField(storage, std.log.levelInfo(), "started", field.value)
        if entry.has {
            check world.out.write(entry.value)
        }
    }
}
```

## Function Signatures

This catalog is generated from the compiler's standard-library signature table. Use these names exactly; helpers with `T` are generic over the concrete span or item type inferred from the call.

Fetch one module's section instead of this whole catalog with `zero skills get stdlib --topic <prefix>`, for example `zero skills get stdlib --topic std.time`.

### std.args

```text
len() -> usize
get(arg0: usize) -> Maybe<String>
has(arg0: usize) -> Bool
getOr(arg0: usize, arg1: String) -> String
find(arg0: String) -> Maybe<usize>
valueAfter(arg0: String) -> Maybe<String>
parseU32(arg0: usize) -> Maybe<u32>
```

### std.ascii

```text
digitValue(arg0: u8) -> Maybe<u8>
hexValue(arg0: u8) -> Maybe<u8>
isAlnum(arg0: u8) -> Bool
isAlpha(arg0: u8) -> Bool
isDigit(arg0: u8) -> Bool
isHexDigit(arg0: u8) -> Bool
isLower(arg0: u8) -> Bool
isUpper(arg0: u8) -> Bool
isWhitespace(arg0: u8) -> Bool
toLower(arg0: u8) -> u8
toUpper(arg0: u8) -> u8
```

### std.cli

```text
argEquals(arg0: usize, arg1: String) -> Bool
command() -> Maybe<String>
commandOr(arg0: String) -> String
commandEquals(arg0: String) -> Bool
argOr(arg0: usize, arg1: String) -> String
argU32Or(arg0: usize, arg1: u32) -> u32
hasFlag(arg0: String) -> Bool
optionValue(arg0: String) -> Maybe<String>
optionValueOr(arg0: String, arg1: String) -> String
optionU32(arg0: String) -> Maybe<u32>
successExitCode() -> i32
usageExitCode() -> i32
isHelp(arg0: Span<u8>) -> Bool
needsHelp() -> Bool
commandIn2(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
commandIn3(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Bool
formatUsage(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
formatCommand(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
formatOption(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
formatSection(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
formatHelpRow(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
formatHelpRowCustom(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: usize, arg4: usize) -> Maybe<Span<u8>>
formatHelpRowWithWidth(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: usize) -> Maybe<Span<u8>>
formatHelp(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
formatError(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
formatUnknownCommand(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
formatMissingOperand(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
formatInvalidOption(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
```

### std.codec

```text
crc32(arg0: String) -> u32
crc32Bytes(arg0: Span<u8>) -> u32
encodedVarintLen(arg0: u32) -> usize
encodedVarintLen64(arg0: u64) -> usize
encodedSignedVarintLen(arg0: i32) -> usize
encodedSignedVarintLen64(arg0: i64) -> usize
hexDecodedLen(arg0: Span<u8>) -> Maybe<usize>
hexDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base32EncodedLen(arg0: usize) -> usize
base32Encode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base32DecodedLen(arg0: Span<u8>) -> Maybe<usize>
base32Decode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base32RawEncodedLen(arg0: usize) -> usize
base32RawEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base32RawDecodedLen(arg0: Span<u8>) -> Maybe<usize>
base32RawDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64DecodedLen(arg0: Span<u8>) -> Maybe<usize>
base64Decode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64RawEncodedLen(arg0: usize) -> usize
base64RawEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64RawDecodedLen(arg0: Span<u8>) -> Maybe<usize>
base64RawDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64UrlEncodedLen(arg0: usize) -> usize
base64UrlEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
base64UrlDecodedLen(arg0: Span<u8>) -> Maybe<usize>
base64UrlDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
readU16Le(arg0: Span<u8>) -> Maybe<u16>
readU16Be(arg0: Span<u8>) -> Maybe<u16>
readU32Le(arg0: Span<u8>) -> Maybe<u32>
readU32Be(arg0: Span<u8>) -> Maybe<u32>
readU64Le(arg0: Span<u8>) -> Maybe<u64>
readU64Be(arg0: Span<u8>) -> Maybe<u64>
writeU16Le(arg0: MutSpan<u8>, arg1: u16) -> Maybe<Span<u8>>
writeU16Be(arg0: MutSpan<u8>, arg1: u16) -> Maybe<Span<u8>>
writeU32Le(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
writeU32Be(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
writeU64Le(arg0: MutSpan<u8>, arg1: u64) -> Maybe<Span<u8>>
writeU64Be(arg0: MutSpan<u8>, arg1: u64) -> Maybe<Span<u8>>
varintEncode(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
varintDecode(arg0: Span<u8>) -> Maybe<u32>
varintEncode64(arg0: MutSpan<u8>, arg1: u64) -> Maybe<Span<u8>>
varintDecode64(arg0: Span<u8>) -> Maybe<u64>
signedVarintEncode(arg0: MutSpan<u8>, arg1: i32) -> Maybe<Span<u8>>
signedVarintDecode(arg0: Span<u8>) -> Maybe<i32>
signedVarintEncode64(arg0: MutSpan<u8>, arg1: i64) -> Maybe<Span<u8>>
signedVarintDecode64(arg0: Span<u8>) -> Maybe<i64>
base64EncodedLen(arg0: usize) -> usize
base64Encode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<String>
hexEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<String>
utf8Valid(arg0: Span<u8>) -> Bool
urlEncode(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
```

### std.collections

```text
append(storage: MutSpan<T>, len: usize, values: Span<T>) -> usize
clear(storage: Span<T>, len: usize) -> usize
contains(storage: Span<T>, len: usize, value: T) -> Bool
count(storage: Span<T>, len: usize, value: T) -> usize
dequeBack(storage: Span<T>, len: usize) -> Maybe<T>
dequeFront(storage: Span<T>, len: usize) -> Maybe<T>
dequePopBack(storage: Span<T>, len: usize) -> usize
dequePopFront(storage: MutSpan<T>, len: usize) -> usize
dequePushBack(storage: MutSpan<T>, len: usize, value: T) -> usize
dequePushFront(storage: MutSpan<T>, len: usize, value: T) -> usize
fill(storage: MutSpan<T>, len: usize, value: T) -> Bool
first(storage: Span<T>, len: usize) -> Maybe<T>
fixedDeque(storage: MutSpan<T>, len: usize) -> FixedDeque<T>
fixedDequeBack(deque: ref<FixedDeque<T>>) -> Maybe<T>
fixedDequeClear(deque: mutref<FixedDeque<T>>) -> usize
fixedDequeFront(deque: ref<FixedDeque<T>>) -> Maybe<T>
fixedDequeIsFull(deque: ref<FixedDeque<T>>) -> Bool
fixedDequeLen(deque: ref<FixedDeque<T>>) -> usize
fixedDequePopBack(deque: mutref<FixedDeque<T>>) -> Maybe<T>
fixedDequePopFront(deque: mutref<FixedDeque<T>>) -> Maybe<T>
fixedDequePushBack(deque: mutref<FixedDeque<T>>, value: T) -> Bool
fixedDequePushFront(deque: mutref<FixedDeque<T>>, value: T) -> Bool
fixedDequeRemaining(deque: ref<FixedDeque<T>>) -> usize
fixedDequeTruncate(deque: mutref<FixedDeque<T>>, newLen: usize) -> usize
fixedDequeView(deque: ref<FixedDeque<T>>) -> Span<T>
fixedRingBuffer(storage: MutSpan<T>, head: usize, len: usize) -> FixedRingBuffer<T>
fixedRingBufferBack(ring: ref<FixedRingBuffer<T>>) -> Maybe<T>
fixedRingBufferCapacity(ring: ref<FixedRingBuffer<T>>) -> usize
fixedRingBufferClear(ring: mutref<FixedRingBuffer<T>>) -> usize
fixedRingBufferFront(ring: ref<FixedRingBuffer<T>>) -> Maybe<T>
fixedRingBufferGet(ring: ref<FixedRingBuffer<T>>, index: usize) -> Maybe<T>
fixedRingBufferIsFull(ring: ref<FixedRingBuffer<T>>) -> Bool
fixedRingBufferLen(ring: ref<FixedRingBuffer<T>>) -> usize
fixedRingBufferPopBack(ring: mutref<FixedRingBuffer<T>>) -> Maybe<T>
fixedRingBufferPopFront(ring: mutref<FixedRingBuffer<T>>) -> Maybe<T>
fixedRingBufferPushBack(ring: mutref<FixedRingBuffer<T>>, value: T) -> Bool
fixedRingBufferPushFront(ring: mutref<FixedRingBuffer<T>>, value: T) -> Bool
fixedRingBufferRemaining(ring: ref<FixedRingBuffer<T>>) -> usize
fixedRingBufferTruncate(ring: mutref<FixedRingBuffer<T>>, newLen: usize) -> usize
fixedMap(keys: MutSpan<K>, values: MutSpan<V>, len: usize) -> FixedMap<K,V>
fixedMapClear(map: mutref<FixedMap<K,V>>) -> usize
fixedMapContains(map: ref<FixedMap<K,V>>, key: K) -> Bool
fixedMapGet(map: ref<FixedMap<K,V>>, key: K) -> Maybe<V>
fixedMapIndex(map: ref<FixedMap<K,V>>, key: K) -> usize
fixedMapIsFull(map: ref<FixedMap<K,V>>) -> Bool
fixedMapKeys(map: ref<FixedMap<K,V>>) -> Span<K>
fixedMapLen(map: ref<FixedMap<K,V>>) -> usize
fixedMapPut(map: mutref<FixedMap<K,V>>, key: K, value: V) -> Bool
fixedMapRemaining(map: ref<FixedMap<K,V>>) -> usize
fixedMapRemove(map: mutref<FixedMap<K,V>>, key: K) -> Bool
fixedMapTruncate(map: mutref<FixedMap<K,V>>, newLen: usize) -> usize
fixedMapValues(map: ref<FixedMap<K,V>>) -> Span<V>
fixedSet(storage: MutSpan<T>, len: usize) -> FixedSet<T>
fixedSetClear(set: mutref<FixedSet<T>>) -> usize
fixedSetContains(set: ref<FixedSet<T>>, value: T) -> Bool
fixedSetInsert(set: mutref<FixedSet<T>>, value: T) -> Bool
fixedSetIsFull(set: ref<FixedSet<T>>) -> Bool
fixedSetLen(set: ref<FixedSet<T>>) -> usize
fixedSetRemaining(set: ref<FixedSet<T>>) -> usize
fixedSetRemove(set: mutref<FixedSet<T>>, value: T) -> Bool
fixedSetTruncate(set: mutref<FixedSet<T>>, newLen: usize) -> usize
fixedSetView(set: ref<FixedSet<T>>) -> Span<T>
insertAt(storage: MutSpan<T>, len: usize, index: usize, value: T) -> usize
insertUnique(storage: MutSpan<T>, len: usize, value: T) -> usize
isFull(storage: Span<T>, len: usize) -> Bool
last(storage: Span<T>, len: usize) -> Maybe<T>
mapClear(keys: Span<K>, values: Span<V>, len: usize) -> usize
mapContains(keys: Span<K>, len: usize, key: K) -> Bool
mapGet(keys: Span<K>, values: Span<V>, len: usize, key: K) -> Maybe<V>
mapIndex(keys: Span<K>, len: usize, key: K) -> usize
mapIsFull(keys: Span<K>, values: Span<V>, len: usize) -> Bool
mapKeys(keys: Span<K>, len: usize) -> Span<K>
mapPut(keys: MutSpan<K>, values: MutSpan<V>, len: usize, key: K, value: V) -> usize
mapRemaining(keys: Span<K>, values: Span<V>, len: usize) -> usize
mapRemove(keys: MutSpan<K>, values: MutSpan<V>, len: usize, key: K) -> usize
mapTruncate(keys: Span<K>, values: Span<V>, len: usize, newLen: usize) -> usize
mapValues(keys: Span<K>, values: Span<V>, len: usize) -> Span<V>
moveToFront(storage: MutSpan<T>, len: usize, index: usize) -> usize
pop(storage: Span<T>, len: usize) -> usize
push(storage: MutSpan<T>, len: usize, value: T) -> usize
remaining(storage: Span<T>, len: usize) -> usize
replaceAt(storage: MutSpan<T>, len: usize, index: usize, value: T) -> Bool
removeAt(storage: MutSpan<T>, len: usize, index: usize) -> usize
removeValue(storage: MutSpan<T>, len: usize, value: T) -> usize
removeSwap(storage: MutSpan<T>, len: usize, index: usize) -> usize
reverse(storage: MutSpan<T>, len: usize) -> Bool
rotateLeft(storage: MutSpan<T>, len: usize, count: usize) -> Bool
rotateRight(storage: MutSpan<T>, len: usize, count: usize) -> Bool
setClear(storage: Span<T>, len: usize) -> usize
setContains(storage: Span<T>, len: usize, value: T) -> Bool
setInsert(storage: MutSpan<T>, len: usize, value: T) -> usize
setIsFull(storage: Span<T>, len: usize) -> Bool
setRemaining(storage: Span<T>, len: usize) -> usize
setRemove(storage: MutSpan<T>, len: usize, value: T) -> usize
setTruncate(storage: Span<T>, len: usize, newLen: usize) -> usize
setView(storage: Span<T>, len: usize) -> Span<T>
swapAt(storage: MutSpan<T>, len: usize, left: usize, right: usize) -> Bool
truncate(storage: Span<T>, len: usize, newLen: usize) -> usize
view(storage: Span<T>, len: usize) -> Span<T>
```

### std.crypto

```text
hash32(arg0: Span<u8>) -> u32
hmac32(arg0: Span<u8>, arg1: Span<u8>) -> u32
constantTimeEql(arg0: Span<u8>, arg1: Span<u8>) -> Bool
secureRandomU32() -> u32
fixedHex32(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
hashHex32(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
hmacHex32(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
stableId32(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
randomId32(arg0: MutSpan<u8>) -> Maybe<Span<u8>>
sha256(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
sha256Hex(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
hmacSha256(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
hmacSha256Hex(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
```

### std.csv

```text
valid(arg0: Span<u8>) -> Bool
recordCount(arg0: Span<u8>) -> Maybe<usize>
record(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
fieldCount(arg0: Span<u8>) -> Maybe<usize>
field(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
encodedFieldLen(arg0: Span<u8>) -> usize
writeField(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeRecord2(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeRecord3(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
```

### std.diag

```text
column(arg0: Span<u8>, arg1: usize) -> usize
formatLocation(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize, arg3: usize) -> Maybe<Span<u8>>
formatOffsetLocation(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: usize) -> Maybe<Span<u8>>
line(arg0: Span<u8>, arg1: usize) -> usize
lineEnd(arg0: Span<u8>, arg1: usize) -> usize
lineStart(arg0: Span<u8>, arg1: usize) -> usize
lineText(arg0: Span<u8>, arg1: usize) -> Span<u8>
rangeLen(arg0: Span<u8>, arg1: usize, arg2: usize) -> usize
rangeText(arg0: Span<u8>, arg1: usize, arg2: usize) -> Span<u8>
```

### std.env

```text
get(arg0: String) -> Maybe<String>
has(arg0: String) -> Bool
getOr(arg0: String, arg1: String) -> String
equals(arg0: String, arg1: String) -> Bool
parseBool(arg0: String) -> Maybe<Bool>
parseBoolOr(arg0: String, arg1: Bool) -> Bool
parseI32(arg0: String) -> Maybe<i32>
parseI32Or(arg0: String, arg1: i32) -> i32
parseU32(arg0: String) -> Maybe<u32>
parseU32Or(arg0: String, arg1: u32) -> u32
parseUsize(arg0: String) -> Maybe<usize>
parseUsizeOr(arg0: String, arg1: usize) -> usize
```

### std.fmt

```text
bool(arg0: MutSpan<u8>, arg1: Bool) -> Maybe<Span<u8>>
hexLowerU32(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
i32(arg0: MutSpan<u8>, arg1: i32) -> Maybe<Span<u8>>
i32Base(arg0: MutSpan<u8>, arg1: i32, arg2: u32) -> Maybe<Span<u8>>
i32Sign(arg0: MutSpan<u8>, arg1: i32) -> Maybe<Span<u8>>
i64(arg0: MutSpan<u8>, arg1: i64) -> Maybe<Span<u8>>
i64Base(arg0: MutSpan<u8>, arg1: i64, arg2: u32) -> Maybe<Span<u8>>
i64Sign(arg0: MutSpan<u8>, arg1: i64) -> Maybe<Span<u8>>
padLeft(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize, arg3: u8) -> Maybe<Span<u8>>
padRight(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize, arg3: u8) -> Maybe<Span<u8>>
u32(arg0: MutSpan<u8>, arg1: u32) -> Maybe<Span<u8>>
u32Base(arg0: MutSpan<u8>, arg1: u32, arg2: u32) -> Maybe<Span<u8>>
u64(arg0: MutSpan<u8>, arg1: u64) -> Maybe<Span<u8>>
u64Base(arg0: MutSpan<u8>, arg1: u64, arg2: u32) -> Maybe<Span<u8>>
usize(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
usizeBase(arg0: MutSpan<u8>, arg1: usize, arg2: u32) -> Maybe<Span<u8>>
writeBool(arg0: mutref<FixedWriter>, arg1: Bool) -> Bool
writeI32(arg0: mutref<FixedWriter>, arg1: i32) -> Bool
writeI32Sign(arg0: mutref<FixedWriter>, arg1: i32) -> Bool
writeI64(arg0: mutref<FixedWriter>, arg1: i64) -> Bool
writeI64Sign(arg0: mutref<FixedWriter>, arg1: i64) -> Bool
writeSpan(arg0: mutref<FixedWriter>, arg1: Span<u8>) -> Bool
writeU32(arg0: mutref<FixedWriter>, arg1: u32) -> Bool
writeU64(arg0: mutref<FixedWriter>, arg1: u64) -> Bool
writeUsize(arg0: mutref<FixedWriter>, arg1: usize) -> Bool
```

### std.fs

```text
host() -> Fs
open(arg0: Fs, arg1: String) -> Maybe<owned<File>>
openOrRaise(arg0: Fs, arg1: String) -> owned<File> raises [NotFound, TooLarge, Io]
create(arg0: Fs, arg1: String) -> Maybe<owned<File>>
createOrRaise(arg0: Fs, arg1: String) -> owned<File> raises [NotFound, TooLarge, Io]
read(arg0: String, arg1: MutSpan<u8>) -> usize
readOrRaise(arg0: mutref<File>, arg1: MutSpan<u8>) -> usize raises [NotFound, TooLarge, Io]
write(arg0: String, arg1: String) -> usize
writeAll(arg0: mutref<File>, arg1: Span<u8>) -> Bool
writeAllOrRaise(arg0: mutref<File>, arg1: Span<u8>) -> Void raises [NotFound, TooLarge, Io]
fileLenOrRaise(arg0: mutref<File>) -> usize raises [NotFound, TooLarge, Io]
readAll(allocator: Alloc, fs: Fs, path: String, max: usize) -> Maybe<owned<ByteBuf>>
readAllOrRaise(allocator: Alloc, fs: Fs, path: String, max: usize) -> owned<ByteBuf> raises [NotFound, TooLarge, Io]
exists(arg0: String) -> Bool
readBytes(arg0: String, arg1: MutSpan<u8>) -> Maybe<usize>
readBytesAt(arg0: String, arg1: usize, arg2: MutSpan<u8>) -> Maybe<usize>
writeBytes(arg0: String, arg1: Span<u8>) -> Maybe<usize>
appendBytes(arg0: String, arg1: Span<u8>) -> Maybe<usize>
isDir(arg0: String) -> Bool
makeDir(arg0: String) -> Bool
removeDir(arg0: String) -> Bool
remove(arg0: String) -> Bool
rename(arg0: String, arg1: String) -> Bool
dirEntryCount(arg0: String) -> Maybe<usize>
dirEntryName(arg0: MutSpan<u8>, arg1: String, arg2: usize) -> Maybe<Span<u8>>
tempName(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
atomicWrite(arg0: String, arg1: String, arg2: Span<u8>) -> Bool
fileLen(arg0: mutref<File>) -> Maybe<usize>
close(arg0: mutref<File>) -> Void
readFile(arg0: Fs, arg1: String, arg2: MutSpan<u8>) -> Maybe<usize>
writeFile(arg0: Fs, arg1: String, arg2: Span<u8>) -> Bool
appendFile(arg0: Fs, arg1: String, arg2: Span<u8>) -> Bool
readFileBytes(arg0: Fs, arg1: String, arg2: MutSpan<u8>) -> Maybe<Span<u8>>
readFileEquals(arg0: Fs, arg1: String, arg2: MutSpan<u8>, arg3: Span<u8>) -> Bool
copyFile(arg0: String, arg1: String, arg2: MutSpan<u8>) -> Bool
fileSize(arg0: Fs, arg1: String) -> Maybe<usize>
isFile(arg0: String) -> Bool
ensureDir(arg0: String) -> Bool
```

`readBytes` and `readFile` fill the caller buffer and return the TOTAL file size
(snprintf convention): a value above `len(buffer)` means the buffer holds only the
first `len(buffer)` bytes, so compare the result against the buffer length instead
of assuming the whole file arrived. `readFileBytes` returns `null` when the file
exceeds the buffer. Process files larger than your buffer with `readBytesAt`:
loop `offset += len(buffer)` until `offset` reaches the returned total, taking
`min(len(buffer), total - offset)` valid bytes per chunk.

### std.http

```text
parseMethod(arg0: String) -> HttpMethod
client(arg0: Net) -> HttpClient
server(arg0: Net, arg1: Address) -> HttpServer
listen(arg0: World, arg1: u16) -> Void raises [Io]
fetch(arg0: HttpClient, arg1: Span<u8>, arg2: MutSpan<u8>, arg3: Duration) -> HttpResult
resultOk(arg0: HttpResult) -> Bool
resultStatus(arg0: HttpResult) -> u16
resultBodyLen(arg0: HttpResult) -> usize
resultError(arg0: HttpResult) -> HttpError
errorNone() -> HttpError
errorInvalidUrl() -> HttpError
errorUnsupportedProtocol() -> HttpError
errorDns() -> HttpError
errorConnect() -> HttpError
errorTls() -> HttpError
errorTimeout() -> HttpError
errorTooLarge() -> HttpError
errorProviderUnavailable() -> HttpError
errorIo() -> HttpError
errorInvalidRequest() -> HttpError
errorName(arg0: HttpError) -> String
responseLen(arg0: Span<u8>) -> usize
responseHeadersLen(arg0: Span<u8>) -> usize
responseBodyOffset(arg0: Span<u8>) -> usize
headerValue(arg0: Span<u8>, arg1: Span<u8>) -> HttpHeaderValue
headerFound(arg0: HttpHeaderValue) -> Bool
headerOffset(arg0: HttpHeaderValue) -> usize
headerLen(arg0: HttpHeaderValue) -> usize
tlsBoundary() -> String
statusReason(arg0: u16) -> String
statusIsInformational(arg0: u16) -> Bool
statusIsSuccess(arg0: u16) -> Bool
statusIsRedirect(arg0: u16) -> Bool
statusIsClientError(arg0: u16) -> Bool
statusIsServerError(arg0: u16) -> Bool
writeRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeMethodRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeGetRequest(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeHeadRequest(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeDeleteRequest(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeJsonMethodRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writePostJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writePutJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writePatchJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeJsonResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeResponseWithHeader(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeResponseWithHeaders(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonResponseWithHeader(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonResponseWithHeaders(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonResponseWithCookie(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonError(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeCorsPreflight(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeCorsJsonResponse(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeTextResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeTextOk(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeHtmlResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeHtmlOk(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeRedirect(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>) -> Maybe<Span<u8>>
writeFound(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeSeeOther(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeMovedPermanently(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writePermanentRedirect(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
contentTypeForPath(arg0: Span<u8>) -> String
writeStaticResponse(arg0: MutSpan<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonOk(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonCreated(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonBadRequest(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonUnauthorized(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonForbidden(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonNotFound(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonMethodNotAllowed(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonConflict(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonUnprocessable(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonTooManyRequests(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeJsonInternalServerError(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeNoContent(arg0: MutSpan<u8>) -> Maybe<Span<u8>>
writeRequestWithHeader(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeRequestWithHeaders(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonRequestWithHeader(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeJsonRequestWithHeaders(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
requestMethodName(arg0: Span<u8>) -> Maybe<Span<u8>>
requestTarget(arg0: Span<u8>) -> Maybe<Span<u8>>
requestPath(arg0: Span<u8>) -> Maybe<Span<u8>>
pathSegmentCount(arg0: Span<u8>) -> usize
pathSegment(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
pathMatchesPattern(arg0: Span<u8>, arg1: Span<u8>) -> Bool
pathParam(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
requestPathSegmentCount(arg0: Span<u8>) -> usize
requestPathSegment(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
requestQuery(arg0: Span<u8>) -> Maybe<Span<u8>>
requestQueryValue(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestHeader(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestBearerToken(arg0: Span<u8>) -> Maybe<Span<u8>>
requestCookie(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestContentLength(arg0: Span<u8>) -> Maybe<usize>
requestContentType(arg0: Span<u8>) -> Maybe<Span<u8>>
requestAccepts(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestAcceptsJson(arg0: Span<u8>) -> Bool
requestBody(arg0: Span<u8>) -> Maybe<Span<u8>>
requestBodyWithin(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
requestHasJsonContentType(arg0: Span<u8>) -> Bool
requestJsonBodyWithin(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
requestJsonField(arg0: Span<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
requestMatches(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
methodAllowed(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestMethodAllowed(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestMethodIs(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsGet(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsHead(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsOptions(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsPost(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsPut(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsPatch(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestIsDelete(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestPathStartsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
requestPathTailAfter(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
requestRouteMatches(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
requestRouteMethodAllowed(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
requestPathParam(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
headerBlockSafe(arg0: Span<u8>) -> Bool
headerBytes(arg0: Span<u8>, arg1: HttpHeaderValue) -> Maybe<Span<u8>>
responseBody(arg0: Span<u8>, arg1: HttpResult) -> Maybe<Span<u8>>
responseBodyBytes(arg0: Span<u8>) -> Maybe<Span<u8>>
responseBodyEquals(arg0: Span<u8>, arg1: Span<u8>) -> Bool
responseStatus(arg0: Span<u8>) -> Maybe<u16>
responseStatusIs(arg0: Span<u8>, arg1: u16) -> Bool
responseHeader(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
responseContentType(arg0: Span<u8>) -> Maybe<Span<u8>>
responseRedirectLocation(arg0: Span<u8>) -> Maybe<Span<u8>>
responseMatches(arg0: Span<u8>, arg1: u16, arg2: Span<u8>, arg3: Span<u8>) -> Bool
testRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
testJsonRequest(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
```

### std.io

```text
bufferedReader(arg0: MutSpan<u8>) -> BufferedReader
bufferedWriter(arg0: MutSpan<u8>) -> BufferedWriter
readerCapacity(arg0: ref<BufferedReader>) -> usize
writerCapacity(arg0: ref<BufferedWriter>) -> usize
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> usize
copyN(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<usize>
copyBuffer(arg0: mutref<FixedReader>, arg1: mutref<FixedWriter>, arg2: MutSpan<u8>) -> usize
copyReaderN(arg0: mutref<FixedReader>, arg1: mutref<FixedWriter>, arg2: usize, arg3: MutSpan<u8>) -> Maybe<usize>
discard(arg0: mutref<FixedReader>, arg1: MutSpan<u8>) -> usize
errorCapacity() -> u32
errorEof() -> u32
errorIo() -> u32
errorName(arg0: u32) -> String
errorNone() -> u32
errorPermission() -> u32
errorShortRead() -> u32
errorShortWrite() -> u32
errorTimeout() -> u32
fixedReader(arg0: Span<u8>, arg1: usize) -> FixedReader
fixedReaderCursor(arg0: ref<FixedReader>) -> usize
fixedReaderDone(arg0: ref<FixedReader>) -> Bool
fixedReaderLen(arg0: ref<FixedReader>) -> usize
fixedReaderLimit(arg0: ref<FixedReader>, arg1: usize) -> FixedReader
fixedReaderRead(arg0: mutref<FixedReader>, arg1: MutSpan<u8>) -> usize
fixedReaderReadAll(arg0: mutref<FixedReader>, arg1: MutSpan<u8>) -> Maybe<Span<u8>>
fixedReaderReadAt(arg0: ref<FixedReader>, arg1: usize, arg2: MutSpan<u8>) -> Maybe<usize>
fixedReaderReadByte(arg0: mutref<FixedReader>) -> Maybe<u8>
fixedReaderReadExact(arg0: mutref<FixedReader>, arg1: MutSpan<u8>) -> Bool
fixedReaderReadLine(arg0: mutref<FixedReader>) -> Maybe<Span<u8>>
fixedReaderReadUntilDelimiter(arg0: mutref<FixedReader>, arg1: u8) -> Maybe<Span<u8>>
fixedReaderRemaining(arg0: ref<FixedReader>) -> usize
fixedReaderSeek(arg0: mutref<FixedReader>, arg1: usize) -> Bool
fixedWriter(arg0: MutSpan<u8>, arg1: usize) -> FixedWriter
fixedWriterCapacity(arg0: ref<FixedWriter>) -> usize
fixedWriterClear(arg0: mutref<FixedWriter>) -> usize
fixedWriterCursor(arg0: ref<FixedWriter>) -> usize
fixedWriterRemaining(arg0: ref<FixedWriter>) -> usize
fixedWriterSeek(arg0: mutref<FixedWriter>, arg1: usize) -> Bool
fixedWriterTruncate(arg0: mutref<FixedWriter>, arg1: usize) -> usize
fixedWriterView(arg0: ref<FixedWriter>) -> Span<u8>
fixedWriterWrite(arg0: mutref<FixedWriter>, arg1: Span<u8>) -> Bool
fixedWriterWriteAll(arg0: mutref<FixedWriter>, arg1: Span<u8>) -> Bool
fixedWriterWriteAt(arg0: mutref<FixedWriter>, arg1: usize, arg2: Span<u8>) -> Bool
fixedWriterWriteByte(arg0: mutref<FixedWriter>, arg1: u8) -> Bool
multiRead(arg0: mutref<FixedReader>, arg1: mutref<FixedReader>, arg2: MutSpan<u8>) -> usize
read(arg0: Span<u8>, arg1: usize, arg2: MutSpan<u8>) -> Maybe<usize>
readAll(arg0: Span<u8>, arg1: MutSpan<u8>) -> Maybe<Span<u8>>
readAt(arg0: Span<u8>, arg1: usize, arg2: MutSpan<u8>) -> Maybe<usize>
readByte(arg0: Span<u8>, arg1: usize) -> Maybe<u8>
readExact(arg0: Span<u8>, arg1: usize, arg2: MutSpan<u8>) -> Maybe<usize>
readLine(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
readLineStart(arg0: Span<u8>, arg1: usize) -> usize
readUntilDelimiter(arg0: Span<u8>, arg1: usize, arg2: u8) -> Maybe<Span<u8>>
readUntilDelimiterStart(arg0: Span<u8>, arg1: usize, arg2: u8) -> usize
teeRead(arg0: mutref<FixedReader>, arg1: mutref<FixedWriter>, arg2: MutSpan<u8>) -> Maybe<usize>
writeByte(arg0: MutSpan<u8>, arg1: usize, arg2: u8) -> Maybe<usize>
writeAll(arg0: MutSpan<u8>, arg1: usize, arg2: Span<u8>) -> Maybe<usize>
writeAt(arg0: MutSpan<u8>, arg1: usize, arg2: Span<u8>) -> Maybe<usize>
writeSpan(arg0: MutSpan<u8>, arg1: usize, arg2: Span<u8>) -> Maybe<usize>
written(arg0: Span<u8>, arg1: usize) -> Span<u8>
remaining(arg0: Span<u8>, arg1: usize) -> usize
nextLine(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
nextLineStart(arg0: Span<u8>, arg1: usize) -> usize
countLines(arg0: Span<u8>) -> usize
```

### std.inet

```text
isIpv4(text: Span<u8>) -> Bool
parseIpv4(text: Span<u8>) -> Maybe<u32>
writeIpv4(buffer: MutSpan<u8>, value: u32) -> Maybe<Span<u8>>
isIpv4Unspecified(value: u32) -> Bool
isIpv4Loopback(value: u32) -> Bool
isIpv4Private(value: u32) -> Bool
isIpv4LinkLocal(value: u32) -> Bool
isIpv4Multicast(value: u32) -> Bool
isIpv6(text: Span<u8>) -> Bool
parseIpv6(buffer: MutSpan<u8>, text: Span<u8>) -> Maybe<Span<u8>>
isIp(text: Span<u8>) -> Bool
parseIp(buffer: MutSpan<u8>, text: Span<u8>) -> Maybe<Span<u8>>
isIpv6Unspecified(bytes: Span<u8>) -> Bool
isIpv6Loopback(bytes: Span<u8>) -> Bool
isIpv6Multicast(bytes: Span<u8>) -> Bool
isIpv6LinkLocal(bytes: Span<u8>) -> Bool
isIpv6Private(bytes: Span<u8>) -> Bool
isIpv6UniqueLocal(bytes: Span<u8>) -> Bool
isIpv6MappedIpv4(bytes: Span<u8>) -> Bool
ipv6MappedIpv4(bytes: Span<u8>) -> Maybe<u32>
isHostname(text: Span<u8>) -> Bool
```

Internet address literal helpers, kept separate from `std.net` so they stay
usable on targets without the Net capability. `isIpv4`/`parseIpv4` accept
strict dotted quads (four 0-255 octets, no leading zeros; the parse packs
big-endian), and `writeIpv4` writes a packed value back to dotted-quad text.
IPv4 classification helpers cover unspecified, loopback, RFC 1918 private,
link-local, and multicast ranges. `isIpv6`/`parseIpv6` accept RFC 4291 forms
including `::` compression and embedded IPv4, writing 16 network-order bytes
into the caller buffer. IPv6 helpers classify unspecified, loopback,
multicast, link-local, unique-local/private, and IPv4-mapped addresses;
`parseIp` accepts either family and writes 4 or 16 network-order bytes.
`isHostname` enforces RFC 1123: dot-separated labels of 1-63 alphanumeric/hyphen
bytes, no leading/trailing hyphens, 253 bytes total.

### std.json

```text
validate(arg0: String) -> Bool
validateBytes(arg0: Span<u8>) -> Bool
parse(allocator: Alloc, text: String) -> Maybe<JsonDoc>
parseBytes(allocator: Alloc, bytes: Span<u8>) -> Maybe<JsonDoc>
streamTokens(arg0: String) -> usize
streamTokensBytes(arg0: Span<u8>) -> usize
writeString(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
decodeBoundary() -> String
errorNone() -> u32
errorInvalid() -> u32
errorTrailing() -> u32
errorName(arg0: u32) -> String
errorExpected(arg0: u32) -> String
validateError(arg0: Span<u8>) -> u32
errorOffset(arg0: Span<u8>) -> usize
errorLine(arg0: Span<u8>) -> usize
errorColumn(arg0: Span<u8>) -> usize
field(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
objectFieldCount(arg0: Span<u8>) -> Maybe<usize>
objectKey(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
objectValue(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
arrayCount(arg0: Span<u8>) -> Maybe<usize>
arrayValue(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
path(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
pathString(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
pathU32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<u32>
pathBool(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Bool>
stringDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
string(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
u32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<u32>
bool(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Bool>
writeStringBytes(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
writeObject1String(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeObject1U32(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: u32) -> Maybe<Span<u8>>
writeObject1Bool(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Bool) -> Maybe<Span<u8>>
writeFieldRaw(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeFieldString(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeFieldU32(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: u32) -> Maybe<Span<u8>>
writeFieldBool(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Bool) -> Maybe<Span<u8>>
writeObject2Fields(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeObject2StringField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
writeObject2U32Field(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: u32, arg3: Span<u8>) -> Maybe<Span<u8>>
writeObject2BoolField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Bool, arg3: Span<u8>) -> Maybe<Span<u8>>
writeArray2Strings(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeArray2U32(arg0: MutSpan<u8>, arg1: u32, arg2: u32) -> Maybe<Span<u8>>
writeArray2Bools(arg0: MutSpan<u8>, arg1: Bool, arg2: Bool) -> Maybe<Span<u8>>
```

### std.toml

```text
validate(arg0: String) -> Bool
validateBytes(arg0: Span<u8>) -> Bool
field(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
stringDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
string(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
u32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<u32>
i32(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<i32>
bool(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Bool>
arrayCount(arg0: Span<u8>) -> Maybe<usize>
arrayValue(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
arrayString(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
arrayU32(arg0: Span<u8>, arg1: usize) -> Maybe<u32>
arrayI32(arg0: Span<u8>, arg1: usize) -> Maybe<i32>
arrayBool(arg0: Span<u8>, arg1: usize) -> Maybe<Bool>
writeKeyValueString(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeKeyValueU32(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: u32) -> Maybe<Span<u8>>
writeKeyValueBool(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Bool) -> Maybe<Span<u8>>
writeTableHeader(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
```

### std.log

```text
message(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
keyValue(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
levelDebug() -> String
levelInfo() -> String
levelWarn() -> String
levelError() -> String
stringField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
messageField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
redacted(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
```

### std.term

```text
reset() -> String
bold() -> String
dim() -> String
underline() -> String
inverse() -> String
fgDefault() -> String
fgBlack() -> String
fgRed() -> String
fgGreen() -> String
fgYellow() -> String
fgBlue() -> String
fgMagenta() -> String
fgCyan() -> String
fgWhite() -> String
bgDefault() -> String
bgBlack() -> String
bgRed() -> String
bgGreen() -> String
bgYellow() -> String
bgBlue() -> String
bgMagenta() -> String
bgCyan() -> String
bgWhite() -> String
clearScreen() -> String
clearScreenDown() -> String
clearScreenUp() -> String
clearLine() -> String
clearLineRight() -> String
clearLineLeft() -> String
cursorHome() -> String
cursorTo(arg0: MutSpan<u8>, arg1: usize, arg2: usize) -> Maybe<Span<u8>>
cursorUp(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
cursorDown(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
cursorRight(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
cursorLeft(arg0: MutSpan<u8>, arg1: usize) -> Maybe<Span<u8>>
saveCursor() -> String
restoreCursor() -> String
hideCursor() -> String
showCursor() -> String
enterAltScreen() -> String
leaveAltScreen() -> String
enterBracketedPaste() -> String
leaveBracketedPaste() -> String
enterMouseCapture() -> String
leaveMouseCapture() -> String
keyNone() -> u32
keyEscape() -> u32
keyEnter() -> u32
keyTab() -> u32
keyBackspace() -> u32
keyCtrlA() -> u32
keyCtrlC() -> u32
keyCtrlD() -> u32
keyCtrlE() -> u32
keyCtrlK() -> u32
keyCtrlL() -> u32
keyCtrlN() -> u32
keyCtrlP() -> u32
keyCtrlR() -> u32
keyCtrlU() -> u32
keyCtrlW() -> u32
keyArrowUp() -> u32
keyArrowDown() -> u32
keyArrowRight() -> u32
keyArrowLeft() -> u32
keyDelete() -> u32
keyHome() -> u32
keyEnd() -> u32
keyPageUp() -> u32
keyPageDown() -> u32
keyInsert() -> u32
keyShiftTab() -> u32
keyF1() -> u32
keyF2() -> u32
keyF3() -> u32
keyF4() -> u32
keyF5() -> u32
keyF6() -> u32
keyF7() -> u32
keyF8() -> u32
keyF9() -> u32
keyF10() -> u32
keyF11() -> u32
keyF12() -> u32
keyPasteStart() -> u32
keyPasteEnd() -> u32
keyCode(arg0: Span<u8>) -> u32
keyByteLen(arg0: Span<u8>) -> usize
stdinIsTty() -> Bool
stdoutIsTty() -> Bool
widthOr(arg0: usize) -> usize
heightOr(arg0: usize) -> usize
enterRawMode() -> Bool
leaveRawMode() -> Bool
readInput(arg0: MutSpan<u8>) -> Maybe<usize>
```

`std.term.readInput(buffer)` fills the caller buffer with currently available
stdin bytes without blocking. It returns `null` when no bytes are available, the
buffer is empty, or the hosted input source cannot be read.

### std.math

```text
absI32(arg0: i32) -> u32
absI64(arg0: i64) -> u64
binomialU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedAddI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedAddU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedAddUsize(arg0: usize, arg1: usize) -> Maybe<usize>
checkedLcmU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedMulI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedMulU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedMulUsize(arg0: usize, arg1: usize) -> Maybe<usize>
checkedPowU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedSubI32(arg0: i32, arg1: i32) -> Maybe<i32>
checkedSubU32(arg0: u32, arg1: u32) -> Maybe<u32>
checkedSubUsize(arg0: usize, arg1: usize) -> Maybe<usize>
clampI32(arg0: i32, arg1: i32, arg2: i32) -> i32
clampI64(arg0: i64, arg1: i64, arg2: i64) -> i64
clampU32(arg0: u32, arg1: u32, arg2: u32) -> u32
clampU64(arg0: u64, arg1: u64, arg2: u64) -> u64
clampUsize(arg0: usize, arg1: usize, arg2: usize) -> usize
divisorCountU32(arg0: u32) -> u32
factorialU32(arg0: u32) -> Maybe<u32>
gcdU32(arg0: u32, arg1: u32) -> u32
isEvenU32(arg0: u32) -> Bool
isOddU32(arg0: u32) -> Bool
isPrimeU32(arg0: u32) -> Bool
lcmU32(arg0: u32, arg1: u32) -> u32
maxI32(arg0: i32, arg1: i32) -> i32
maxI64(arg0: i64, arg1: i64) -> i64
maxU32(arg0: u32, arg1: u32) -> u32
maxU64(arg0: u64, arg1: u64) -> u64
maxUsize(arg0: usize, arg1: usize) -> usize
minI32(arg0: i32, arg1: i32) -> i32
minI64(arg0: i64, arg1: i64) -> i64
minU32(arg0: u32, arg1: u32) -> u32
minU64(arg0: u64, arg1: u64) -> u64
minUsize(arg0: usize, arg1: usize) -> usize
modPowU32(arg0: u32, arg1: u32, arg2: u32) -> u32
powU32(arg0: u32, arg1: u32) -> u32
properDivisorSumU32(arg0: u32) -> u32
saturatingAddI32(arg0: i32, arg1: i32) -> i32
saturatingAddU32(arg0: u32, arg1: u32) -> u32
saturatingAddUsize(arg0: usize, arg1: usize) -> usize
saturatingMulI32(arg0: i32, arg1: i32) -> i32
saturatingMulU32(arg0: u32, arg1: u32) -> u32
saturatingMulUsize(arg0: usize, arg1: usize) -> usize
saturatingSubI32(arg0: i32, arg1: i32) -> i32
saturatingSubU32(arg0: u32, arg1: u32) -> u32
saturatingSubUsize(arg0: usize, arg1: usize) -> usize
sqrtFloorU32(arg0: u32) -> u32
```

### std.mem

```text
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> usize
copyItems(dst: MutSpan<T>, src: Span<T>) -> usize
fill(arg0: MutSpan<u8>, arg1: u8) -> usize
fillItems(dst: MutSpan<T>, value: T) -> usize
eql(arg0: String, arg1: String) -> Bool
span(arg0: String) -> Span<u8>
contains(items: Span<T>, value: T) -> Bool
compareI32(arg0: Span<i32>, arg1: Span<i32>) -> i32
compareU8(arg0: Span<u8>, arg1: Span<u8>) -> i32
compareBytes(arg0: Span<u8>, arg1: Span<u8>) -> i32
compareU32(arg0: Span<u32>, arg1: Span<u32>) -> i32
compareUsize(arg0: Span<usize>, arg1: Span<usize>) -> i32
startsWith(items: Span<T>, prefix: Span<T>) -> Bool
endsWith(items: Span<T>, suffix: Span<T>) -> Bool
splitBefore(items: Span<T>, delimiter: T) -> Span<T>
splitAfter(items: Span<T>, delimiter: T) -> Span<T>
isEmpty(items: Span<T>) -> Bool
chunkCount(items: Span<T>, chunkSize: usize) -> usize
chunk(items: Span<T>, chunkIndex: usize, chunkSize: usize) -> Span<T>
windowCount(items: Span<T>, windowSize: usize) -> usize
window(items: Span<T>, windowIndex: usize, windowSize: usize) -> Span<T>
advance(items: Span<T>, cursor: usize, count: usize) -> usize
cursorDone(items: Span<T>, cursor: usize) -> Bool
remaining(items: Span<T>, cursor: usize) -> Span<T>
cursorChunk(items: Span<T>, cursor: usize, count: usize) -> Span<T>
prefix(items: Span<T>, len: usize) -> Span<T>
dropPrefix(items: Span<T>, len: usize) -> Span<T>
suffix(items: Span<T>, len: usize) -> Span<T>
dropSuffix(items: Span<T>, len: usize) -> Span<T>
slice(items: Span<T>, start: usize, len: usize) -> Span<T>
len(items: Span<T>) -> usize
get(items: Span<T>, index: usize) -> Maybe<T>
eqlBytes(left: Span<T>, right: Span<T>) -> Bool
nullAlloc() -> NullAlloc
fixedBufAlloc(arg0: MutSpan<u8>) -> FixedBufAlloc
arena(arg0: MutSpan<u8>) -> FixedBufAlloc
pageAlloc() -> PageAlloc
generalAlloc() -> GeneralAlloc
allocBytes(allocator: Alloc, len: usize) -> Maybe<MutSpan<u8>>
byteBuf(allocator: Alloc, capacity: usize) -> Maybe<owned<ByteBuf>>
vec(arg0: MutSpan<u8>) -> Vec
vecPush(arg0: mutref<Vec>, arg1: u8) -> Bool
vecBytes(arg0: ref<Vec>) -> Span<u8>
vecGet(arg0: ref<Vec>, arg1: usize) -> Maybe<u8>
vecSet(arg0: mutref<Vec>, arg1: usize, arg2: u8) -> Bool
vecClear(arg0: mutref<Vec>) -> usize
vecPop(arg0: mutref<Vec>) -> Bool
vecTruncate(arg0: mutref<Vec>, arg1: usize) -> usize
vecRemoveSwap(arg0: mutref<Vec>, arg1: usize) -> Bool
vecIndex(arg0: ref<Vec>, arg1: u8) -> usize
vecContains(arg0: ref<Vec>, arg1: u8) -> Bool
vecInsertUnique(arg0: mutref<Vec>, arg1: u8) -> Bool
vecRemoveValue(arg0: mutref<Vec>, arg1: u8) -> Bool
vecLen(arg0: ref<Vec>) -> usize
vecCapacity(arg0: ref<Vec>) -> usize
vecRemaining(arg0: ref<Vec>) -> usize
vecIsEmpty(arg0: ref<Vec>) -> Bool
vecIsFull(arg0: ref<Vec>) -> Bool
bufBytes(arg0: ref<ByteBuf>) -> MutSpan<u8>
bufLen(arg0: ref<ByteBuf>) -> usize
reset(arg0: mutref<FixedBufAlloc>) -> Void
capacity(arg0: FixedBufAlloc) -> usize
```

### std.net

```text
host() -> Net
address(arg0: String, arg1: u16) -> Address
dnsName(arg0: Address) -> String
connect(arg0: Net, arg1: Address) -> Maybe<Conn>
listen(arg0: Net, arg1: Address) -> Maybe<Listener>
withTimeout(arg0: Address, arg1: Duration) -> Address
localhost(arg0: u16) -> Address
loopback(arg0: u16) -> Address
```

### std.parse

```text
isAsciiDigit(arg0: Span<u8>) -> Bool
isAsciiAlpha(arg0: Span<u8>) -> Bool
isIdentifierStart(arg0: Span<u8>) -> Bool
isWhitespace(arg0: Span<u8>) -> Bool
scanDigits(arg0: Span<u8>) -> usize
scanIdentifier(arg0: Span<u8>) -> usize
scanUntilByte(arg0: Span<u8>, arg1: u8) -> usize
scanWhitespace(arg0: Span<u8>) -> usize
tokenAscii(arg0: Span<u8>) -> Span<u8>
parseBool(arg0: Span<u8>) -> Maybe<Bool>
parseByteSize(arg0: Span<u8>) -> Maybe<usize>
parseDuration(arg0: Span<u8>) -> Maybe<Duration>
parseI32(arg0: Span<u8>) -> Maybe<i32>
parseI32Base(arg0: Span<u8>, arg1: u32) -> Maybe<i32>
parseI32Prefix(arg0: Span<u8>) -> Maybe<i32>
parseI64(arg0: Span<u8>) -> Maybe<i64>
parseI64Base(arg0: Span<u8>, arg1: u32) -> Maybe<i64>
parseI64Prefix(arg0: Span<u8>) -> Maybe<i64>
parseU8(arg0: Span<u8>) -> Maybe<u8>
parseU16(arg0: Span<u8>) -> Maybe<u16>
parseU32(arg0: Span<u8>) -> Maybe<u32>
parseU32Base(arg0: Span<u8>, arg1: u32) -> Maybe<u32>
parseU32Prefix(arg0: Span<u8>) -> Maybe<u32>
parseU64(arg0: Span<u8>) -> Maybe<u64>
parseU64Base(arg0: Span<u8>, arg1: u32) -> Maybe<u64>
parseU64Prefix(arg0: Span<u8>) -> Maybe<u64>
parseUsize(arg0: Span<u8>) -> Maybe<usize>
parseUsizeBase(arg0: Span<u8>, arg1: u32) -> Maybe<usize>
parseUsizePrefix(arg0: Span<u8>) -> Maybe<usize>
```

### std.path

```text
basename(arg0: String) -> String
dirname(arg0: String) -> String
extension(arg0: String) -> String
stem(arg0: String) -> String
splitDir(arg0: String) -> String
splitBase(arg0: String) -> String
componentCount(arg0: String) -> usize
component(arg0: String, arg1: usize) -> Maybe<String>
isAbs(arg0: String) -> Bool
abs(arg0: MutSpan<u8>, arg1: String, arg2: String) -> Maybe<String>
join(arg0: MutSpan<u8>, arg1: String, arg2: String) -> Maybe<String>
normalize(arg0: MutSpan<u8>, arg1: String) -> Maybe<String>
relative(arg0: MutSpan<u8>, arg1: String, arg2: String) -> Maybe<String>
```

### std.proc

```text
spawn(arg0: String) -> ProcStatus
spawnInherit(arg0: String) -> ProcStatus
spawnInheritArgs(arg0: String, arg1: Span<u8>, arg2: String, arg3: Span<u8>) -> ProcStatus
exitCode(arg0: ProcStatus) -> i32
succeeded(arg0: ProcStatus) -> Bool
failed(arg0: ProcStatus) -> Bool
runOk(arg0: String) -> Bool
runCode(arg0: String) -> i32
capture(arg0: String, arg1: MutSpan<u8>) -> Maybe<usize>
captureArgs(arg0: String, arg1: Span<u8>, arg2: MutSpan<u8>) -> Maybe<usize>
captureFiles(arg0: String, arg1: String, arg2: String) -> ProcStatus
captureFilesArgs(arg0: String, arg1: Span<u8>, arg2: String, arg3: String) -> ProcStatus
spawnChild(arg0: String) -> ProcChild
spawnChildIn(arg0: String, arg1: String) -> ProcChild
spawnChildInEnv(arg0: String, arg1: String, arg2: Span<u8>) -> ProcChild
spawnChildArgs(arg0: String, arg1: Span<u8>, arg2: String, arg3: Span<u8>) -> ProcChild
childValid(arg0: ProcChild) -> Bool
running(arg0: ProcChild) -> Bool
wait(arg0: ProcChild) -> ProcStatus
kill(arg0: ProcChild) -> Bool
interrupt(arg0: ProcChild) -> Bool
close(arg0: ProcChild) -> Bool
closeStdin(arg0: ProcChild) -> Bool
pid(arg0: ProcChild) -> i32
pidRunning(arg0: i32) -> Bool
killPid(arg0: i32) -> Bool
interruptPid(arg0: i32) -> Bool
killGroupPid(arg0: i32) -> Bool
interruptGroupPid(arg0: i32) -> Bool
readStdout(arg0: ProcChild, arg1: MutSpan<u8>) -> Maybe<usize>
readStderr(arg0: ProcChild, arg1: MutSpan<u8>) -> Maybe<usize>
writeStdin(arg0: ProcChild, arg1: Span<u8>) -> Maybe<usize>
```

### std.pty

```text
spawn(arg0: String) -> ProcChild
spawnIn(arg0: String, arg1: String) -> ProcChild
spawnInEnv(arg0: String, arg1: String, arg2: Span<u8>) -> ProcChild
spawnArgs(arg0: String, arg1: Span<u8>, arg2: String, arg3: Span<u8>) -> ProcChild
valid(arg0: ProcChild) -> Bool
childValid(arg0: ProcChild) -> Bool
running(arg0: ProcChild) -> Bool
wait(arg0: ProcChild) -> ProcStatus
kill(arg0: ProcChild) -> Bool
interrupt(arg0: ProcChild) -> Bool
close(arg0: ProcChild) -> Bool
pid(arg0: ProcChild) -> i32
read(arg0: ProcChild, arg1: MutSpan<u8>) -> Maybe<usize>
write(arg0: ProcChild, arg1: Span<u8>) -> Maybe<usize>
resize(arg0: ProcChild, arg1: usize, arg2: usize) -> Bool
```

`std.pty` starts hosted children attached to a pseudoterminal instead of
separate stdin/stdout/stderr pipes. It returns the same `ProcChild` handle shape
as `std.proc`, so `running`, `wait`, `interrupt`, `kill`, `close`, and `pid`
have the same lifecycle meaning. `read` and `write` are nonblocking; `read`
returns `null` when no bytes are currently available or the terminal has closed.
For short-lived PTY children, drain output with `read` before `wait`; once the
child exits, host PTYs may report the terminal as closed.
Use PTY children for interactive programs that need terminal behavior such as
line editing, prompts, color, cursor control, or terminal-size awareness.

### std.rand

```text
seed(arg0: u32) -> RandSource
nextU32(arg0: mutref<RandSource>) -> u32
nextBool(arg0: mutref<RandSource>) -> Bool
nextBelow(arg0: mutref<RandSource>, arg1: u32) -> Maybe<u32>
rangeU32(arg0: mutref<RandSource>, arg1: u32, arg2: u32) -> Maybe<u32>
entropyU32() -> u32
entropySeed() -> RandSource
entropyHex32(arg0: MutSpan<u8>) -> Maybe<Span<u8>>
```

### std.regex

```text
compile(buffer: MutSpan<u8>, pattern: Span<u8>) -> Maybe<Span<u8>>
compileErrorOffset(buffer: MutSpan<u8>, pattern: Span<u8>) -> Maybe<usize>
compileStatus(buffer: MutSpan<u8>, pattern: Span<u8>) -> u32
contains(pattern: Span<u8>, text: Span<u8>) -> Maybe<Bool>
find(pattern: Span<u8>, text: Span<u8>) -> Maybe<Span<u8>>
findCount(pattern: Span<u8>, text: Span<u8>) -> Maybe<usize>
findIndex(pattern: Span<u8>, text: Span<u8>) -> Maybe<usize>
findNth(pattern: Span<u8>, text: Span<u8>, index: usize) -> Maybe<Span<u8>>
findNthIndex(pattern: Span<u8>, text: Span<u8>, index: usize) -> Maybe<usize>
replace(buffer: MutSpan<u8>, pattern: Span<u8>, text: Span<u8>, replacement: Span<u8>) -> Maybe<Span<u8>>
split(pattern: Span<u8>, text: Span<u8>, index: usize) -> Maybe<Span<u8>>
splitCount(pattern: Span<u8>, text: Span<u8>) -> Maybe<usize>
statusName(status: u32) -> String
isMatch(program: Span<u8>, text: Span<u8>) -> Bool
matches(pattern: Span<u8>, text: Span<u8>) -> Maybe<Bool>
```

Supported pattern subset (ECMA-262-leaning syntax, matching by codepoint,
unanchored search like `RegExp.prototype.test`): literals, `.`, classes with negation,
ranges, and `\d \D \w \W \s \S`, anchors `^` `$`, word boundaries `\b` `\B`,
greedy quantifiers `* + ? {m} {m,} {m,n}`, alternation `|`, capturing and
`(?:...)` groups (matching only). Compile once into a caller buffer, then call
`isMatch` repeatedly. Unsupported constructs are compile errors with status
codes: 1 backreference, 2 lookahead, 3 lookbehind, 4 named group, 5 lazy
quantifier, 6 group modifier, 7 unicode property escape, 8 syntax, 9 quantifier
range, 10 over buffer/2048-byte program limit, 11 pattern not UTF-8, 12 nesting
depth over 32. `statusName` names a code for diagnostics. Search, split, and
replace helpers use the leftmost start and longest end for each match; `split`
and `splitCount` use non-empty matches as separators and ignore zero-length
matches.

### std.search

```text
binaryI32(arg0: Span<i32>, arg1: i32) -> usize
binaryDescI32(arg0: Span<i32>, arg1: i32) -> usize
binaryU32(arg0: Span<u32>, arg1: u32) -> usize
binaryDescU32(arg0: Span<u32>, arg1: u32) -> usize
binaryUsize(arg0: Span<usize>, arg1: usize) -> usize
binaryDescUsize(arg0: Span<usize>, arg1: usize) -> usize
containsSortedI32(arg0: Span<i32>, arg1: i32) -> Bool
containsSortedDescI32(arg0: Span<i32>, arg1: i32) -> Bool
containsSortedU32(arg0: Span<u32>, arg1: u32) -> Bool
containsSortedDescU32(arg0: Span<u32>, arg1: u32) -> Bool
containsSortedUsize(arg0: Span<usize>, arg1: usize) -> Bool
containsSortedDescUsize(arg0: Span<usize>, arg1: usize) -> Bool
countSortedI32(arg0: Span<i32>, arg1: i32) -> usize
countSortedDescI32(arg0: Span<i32>, arg1: i32) -> usize
countSortedU32(arg0: Span<u32>, arg1: u32) -> usize
countSortedDescU32(arg0: Span<u32>, arg1: u32) -> usize
countSortedUsize(arg0: Span<usize>, arg1: usize) -> usize
countSortedDescUsize(arg0: Span<usize>, arg1: usize) -> usize
equalRangeI32(arg0: Span<i32>, arg1: i32) -> Span<i32>
equalRangeDescI32(arg0: Span<i32>, arg1: i32) -> Span<i32>
equalRangeU32(arg0: Span<u32>, arg1: u32) -> Span<u32>
equalRangeDescU32(arg0: Span<u32>, arg1: u32) -> Span<u32>
equalRangeUsize(arg0: Span<usize>, arg1: usize) -> Span<usize>
equalRangeDescUsize(arg0: Span<usize>, arg1: usize) -> Span<usize>
partitionPointI32(arg0: Span<i32>, arg1: i32) -> usize
partitionPointDescI32(arg0: Span<i32>, arg1: i32) -> usize
partitionPointU32(arg0: Span<u32>, arg1: u32) -> usize
partitionPointDescU32(arg0: Span<u32>, arg1: u32) -> usize
partitionPointUsize(arg0: Span<usize>, arg1: usize) -> usize
partitionPointDescUsize(arg0: Span<usize>, arg1: usize) -> usize
indexOf(items: Span<T>, value: T) -> usize
lastIndexOf(items: Span<T>, value: T) -> usize
lowerBoundI32(arg0: Span<i32>, arg1: i32) -> usize
lowerBoundDescI32(arg0: Span<i32>, arg1: i32) -> usize
lowerBoundU32(arg0: Span<u32>, arg1: u32) -> usize
lowerBoundDescU32(arg0: Span<u32>, arg1: u32) -> usize
lowerBoundUsize(arg0: Span<usize>, arg1: usize) -> usize
lowerBoundDescUsize(arg0: Span<usize>, arg1: usize) -> usize
minI32(arg0: Span<i32>) -> Maybe<i32>
minU32(arg0: Span<u32>) -> Maybe<u32>
minUsize(arg0: Span<usize>) -> Maybe<usize>
minIndexI32(arg0: Span<i32>) -> usize
minIndexU32(arg0: Span<u32>) -> usize
minIndexUsize(arg0: Span<usize>) -> usize
maxI32(arg0: Span<i32>) -> Maybe<i32>
maxU32(arg0: Span<u32>) -> Maybe<u32>
maxUsize(arg0: Span<usize>) -> Maybe<usize>
maxIndexI32(arg0: Span<i32>) -> usize
maxIndexU32(arg0: Span<u32>) -> usize
maxIndexUsize(arg0: Span<usize>) -> usize
upperBoundI32(arg0: Span<i32>, arg1: i32) -> usize
upperBoundDescI32(arg0: Span<i32>, arg1: i32) -> usize
upperBoundU32(arg0: Span<u32>, arg1: u32) -> usize
upperBoundDescU32(arg0: Span<u32>, arg1: u32) -> usize
upperBoundUsize(arg0: Span<usize>, arg1: usize) -> usize
upperBoundDescUsize(arg0: Span<usize>, arg1: usize) -> usize
```

### std.sort

```text
insertionI32(arg0: MutSpan<i32>) -> Void
insertionDescI32(arg0: MutSpan<i32>) -> Void
insertionU32(arg0: MutSpan<u32>) -> Void
insertionDescU32(arg0: MutSpan<u32>) -> Void
insertionUsize(arg0: MutSpan<usize>) -> Void
insertionDescUsize(arg0: MutSpan<usize>) -> Void
stableI32(arg0: MutSpan<i32>) -> Void
stableU32(arg0: MutSpan<u32>) -> Void
stableUsize(arg0: MutSpan<usize>) -> Void
stableDescI32(arg0: MutSpan<i32>) -> Void
stableDescU32(arg0: MutSpan<u32>) -> Void
stableDescUsize(arg0: MutSpan<usize>) -> Void
unstableI32(arg0: MutSpan<i32>) -> Void
unstableU32(arg0: MutSpan<u32>) -> Void
unstableUsize(arg0: MutSpan<usize>) -> Void
unstableDescI32(arg0: MutSpan<i32>) -> Void
unstableDescU32(arg0: MutSpan<u32>) -> Void
unstableDescUsize(arg0: MutSpan<usize>) -> Void
reverseI32(arg0: MutSpan<i32>) -> Void
swapI32(arg0: MutSpan<i32>, arg1: usize, arg2: usize) -> Bool
rotateLeftI32(arg0: MutSpan<i32>, arg1: usize) -> Void
rotateRightI32(arg0: MutSpan<i32>, arg1: usize) -> Void
reverseU32(arg0: MutSpan<u32>) -> Void
swapU32(arg0: MutSpan<u32>, arg1: usize, arg2: usize) -> Bool
rotateLeftU32(arg0: MutSpan<u32>, arg1: usize) -> Void
rotateRightU32(arg0: MutSpan<u32>, arg1: usize) -> Void
reverseUsize(arg0: MutSpan<usize>) -> Void
swapUsize(arg0: MutSpan<usize>, arg1: usize, arg2: usize) -> Bool
rotateLeftUsize(arg0: MutSpan<usize>, arg1: usize) -> Void
rotateRightUsize(arg0: MutSpan<usize>, arg1: usize) -> Void
isSortedI32(arg0: Span<i32>) -> Bool
isSortedDescI32(arg0: Span<i32>) -> Bool
isSortedU32(arg0: Span<u32>) -> Bool
isSortedDescU32(arg0: Span<u32>) -> Bool
isSortedUsize(arg0: Span<usize>) -> Bool
isSortedDescUsize(arg0: Span<usize>) -> Bool
partitionI32(arg0: MutSpan<i32>, arg1: i32) -> usize
partitionDescI32(arg0: MutSpan<i32>, arg1: i32) -> usize
partitionU32(arg0: MutSpan<u32>, arg1: u32) -> usize
partitionDescU32(arg0: MutSpan<u32>, arg1: u32) -> usize
partitionUsize(arg0: MutSpan<usize>, arg1: usize) -> usize
partitionDescUsize(arg0: MutSpan<usize>, arg1: usize) -> usize
isPartitionedI32(arg0: Span<i32>, arg1: i32) -> Bool
isPartitionedDescI32(arg0: Span<i32>, arg1: i32) -> Bool
isPartitionedU32(arg0: Span<u32>, arg1: u32) -> Bool
isPartitionedDescU32(arg0: Span<u32>, arg1: u32) -> Bool
isPartitionedUsize(arg0: Span<usize>, arg1: usize) -> Bool
isPartitionedDescUsize(arg0: Span<usize>, arg1: usize) -> Bool
selectNthI32(arg0: MutSpan<i32>, arg1: usize) -> Bool
selectNthDescI32(arg0: MutSpan<i32>, arg1: usize) -> Bool
selectNthU32(arg0: MutSpan<u32>, arg1: usize) -> Bool
selectNthDescU32(arg0: MutSpan<u32>, arg1: usize) -> Bool
selectNthUsize(arg0: MutSpan<usize>, arg1: usize) -> Bool
selectNthDescUsize(arg0: MutSpan<usize>, arg1: usize) -> Bool
mergeSortedI32(arg0: MutSpan<i32>, arg1: Span<i32>, arg2: Span<i32>) -> usize
mergeSortedDescI32(arg0: MutSpan<i32>, arg1: Span<i32>, arg2: Span<i32>) -> usize
mergeSortedU32(arg0: MutSpan<u32>, arg1: Span<u32>, arg2: Span<u32>) -> usize
mergeSortedDescU32(arg0: MutSpan<u32>, arg1: Span<u32>, arg2: Span<u32>) -> usize
mergeSortedUsize(arg0: MutSpan<usize>, arg1: Span<usize>, arg2: Span<usize>) -> usize
mergeSortedDescUsize(arg0: MutSpan<usize>, arg1: Span<usize>, arg2: Span<usize>) -> usize
dedupeSortedI32(arg0: MutSpan<i32>) -> usize
dedupeSortedU32(arg0: MutSpan<u32>) -> usize
dedupeSortedUsize(arg0: MutSpan<usize>) -> usize
```

### std.str

```text
contains(arg0: Span<u8>, arg1: Span<u8>) -> Bool
concat(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
copy(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
count(arg0: Span<u8>, arg1: Span<u8>) -> usize
countByte(arg0: Span<u8>, arg1: u8) -> usize
eqlIgnoreAsciiCase(arg0: Span<u8>, arg1: Span<u8>) -> Bool
endsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
fieldAscii(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
fieldCountAscii(arg0: Span<u8>) -> usize
indexOf(arg0: Span<u8>, arg1: Span<u8>) -> usize
lastIndexOf(arg0: Span<u8>, arg1: Span<u8>) -> usize
line(arg0: Span<u8>, arg1: usize) -> Maybe<Span<u8>>
lineCount(arg0: Span<u8>) -> usize
repeat(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
replace(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
reverse(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
split(arg0: Span<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
splitCount(arg0: Span<u8>, arg1: Span<u8>) -> usize
startsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
toLowerAscii(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
toUpperAscii(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
trimAscii(arg0: Span<u8>) -> Span<u8>
trimEndAscii(arg0: Span<u8>) -> Span<u8>
trimStartAscii(arg0: Span<u8>) -> Span<u8>
wordCountAscii(arg0: Span<u8>) -> usize
```

### std.testing

```text
isTrue(arg0: Bool) -> Bool
isFalse(arg0: Bool) -> Bool
equalBool(arg0: Bool, arg1: Bool) -> Bool
equalUsize(arg0: usize, arg1: usize) -> Bool
equalU32(arg0: u32, arg1: u32) -> Bool
equalI32(arg0: i32, arg1: i32) -> Bool
equalBytes(arg0: Span<u8>, arg1: Span<u8>) -> Bool
containsBytes(arg0: Span<u8>, arg1: Span<u8>) -> Bool
startsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
endsWith(arg0: Span<u8>, arg1: Span<u8>) -> Bool
notEqualBytes(arg0: Span<u8>, arg1: Span<u8>) -> Bool
diffIndexBytes(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<usize>
jsonFieldEquals(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
jsonPathEquals(arg0: Span<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Bool
caseName(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: usize) -> Maybe<Span<u8>>
```

### std.text

```text
isAscii(arg0: Span<u8>) -> Bool
utf8Len(arg0: Span<u8>) -> Maybe<usize>
utf8Valid(arg0: Span<u8>) -> Bool
```

### std.time

```text
ns(arg0: i64) -> Duration
us(arg0: i64) -> Duration
ms(arg0: i32) -> Duration
seconds(arg0: i32) -> Duration
minutes(arg0: i32) -> Duration
hours(arg0: i32) -> Duration
zero() -> Duration
add(arg0: Duration, arg1: Duration) -> Duration
monotonic() -> Duration
wallSeconds() -> i64
sub(arg0: Duration, arg1: Duration) -> Duration
asNs(arg0: Duration) -> i64
asUsFloor(arg0: Duration) -> i64
asMsFloor(arg0: Duration) -> i32
asSecondsFloor(arg0: Duration) -> i64
min(arg0: Duration, arg1: Duration) -> Duration
max(arg0: Duration, arg1: Duration) -> Duration
clamp(arg0: Duration, arg1: Duration, arg2: Duration) -> Duration
lessThan(arg0: Duration, arg1: Duration) -> Bool
isZero(arg0: Duration) -> Bool
abs(arg0: Duration) -> Duration
between(arg0: Duration, arg1: Duration) -> Duration
hasElapsed(arg0: Duration, arg1: Duration, arg2: Duration) -> Bool
deadlineAfter(arg0: Duration, arg1: Duration) -> Duration
remainingUntil(arg0: Duration, arg1: Duration) -> Duration
deadlineExpired(arg0: Duration, arg1: Duration) -> Bool
sleep(arg0: Duration) -> Bool
isRfc3339Date(text: Span<u8>) -> Bool
isRfc3339Time(text: Span<u8>) -> Bool
isRfc3339DateTime(text: Span<u8>) -> Bool
parseRfc3339DateTimeOr(text: Span<u8>, fallback: i64) -> i64
isLeapYear(year: u32) -> Bool
daysInMonth(year: u32, month: u32) -> u32
writeDurationNs(arg0: MutSpan<u8>, arg1: Duration) -> Maybe<Span<u8>>
writeDurationMs(arg0: MutSpan<u8>, arg1: Duration) -> Maybe<Span<u8>>
writeDurationSeconds(arg0: MutSpan<u8>, arg1: Duration) -> Maybe<Span<u8>>
```

The RFC 3339 helpers are target-neutral and validate calendar dates (leap
years, days-in-month), times with fractional seconds and numeric offsets, and
date-times joined by `T` or `t`. The leap-second rule is exact: seconds `60`
is valid only when the time normalized by its offset equals `23:59:60` UTC,
wrapping modulo 24 hours (`00:29:60+00:30` is valid; `23:59:60-01:00` is not).
`parseRfc3339DateTimeOr` returns UTC epoch seconds, truncating fractions and
mapping a valid leap second to the same epoch second as `:59`; it returns the
fallback for invalid text.
Use `writeDurationNs`, `writeDurationMs`, or `writeDurationSeconds` when a
typed `Duration` needs a compact textual value in caller-owned storage.
Use `deadlineAfter`, `remainingUntil`, and `deadlineExpired` to model request
budgets against monotonic `Duration` instants without observing the clock inside
the helper.
`std.time.sleep` is hosted and returns `false` on host failure. Timer and
fake-clock handles are not exposed in the current surface.

### std.unicode

```text
decodeAt(text: Span<u8>, index: usize) -> Maybe<u32>
decodeStatusAt(text: Span<u8>, index: usize) -> u32
widthAt(text: Span<u8>, index: usize) -> Maybe<usize>
encode(buffer: MutSpan<u8>, cp: u32) -> Maybe<Span<u8>>
encodedWidth(cp: u32) -> Maybe<usize>
invalidIndex(text: Span<u8>) -> usize
isDigit(cp: u32) -> Bool
isWord(cp: u32) -> Bool
isSpace(cp: u32) -> Bool
nextIndex(text: Span<u8>, index: usize) -> Maybe<usize>
statusName(status: u32) -> String
```

Decoding is strict UTF-8 (overlong encodings, surrogates, values above
U+10FFFF, and truncated sequences return `null`). Iterate codepoints by
advancing a byte index with `nextIndex` or `widthAt`. `invalidIndex` returns
the first invalid byte index or the input length, and `decodeStatusAt` plus
`statusName` provide allocation-free decode diagnostics. The class helpers use
ECMA-262 regex semantics by codepoint (`\d` `\w` `\s`).

### std.url

```text
percentEncode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
percentDecode(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
queryEscape(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
queryUnescape(arg0: MutSpan<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
scheme(arg0: Span<u8>) -> Maybe<Span<u8>>
authority(arg0: Span<u8>) -> Maybe<Span<u8>>
host(arg0: Span<u8>) -> Maybe<Span<u8>>
path(arg0: Span<u8>) -> Span<u8>
query(arg0: Span<u8>) -> Maybe<Span<u8>>
fragment(arg0: Span<u8>) -> Maybe<Span<u8>>
queryValue(arg0: Span<u8>, arg1: Span<u8>) -> Maybe<Span<u8>>
queryValueDecoded(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeQueryParam(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeFormField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
appendFormField(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
formValue(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
appendQuery(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
writeUrl(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>, arg3: Span<u8>) -> Maybe<Span<u8>>
appendFragment(arg0: MutSpan<u8>, arg1: Span<u8>, arg2: Span<u8>) -> Maybe<Span<u8>>
```

## Maybe Pattern

```zero
pub fn main(world: World) -> Void raises {
    let first: Maybe<String> = std.args.get(1)
    if first.has {
        check world.out.write(first.value)
    }
}
```

Use the CLI helpers for command, fallback, exact flag, and option conventions
before writing a custom argument loop:

```zero
pub fn main(world: World) -> Void raises {
    let command: String = std.cli.commandOr("help")
    let name: String = std.cli.argOr(2, "world")
    if std.mem.eql(command, "hello") {
        check world.out.write("hello ")
        check world.out.write(name)
        check world.out.write("\n")
    }
}
```

Use `check maybeValue` only when absence should propagate as a failure in a fallible function.
Read `maybeValue.value` only inside a visible `if maybeValue.has { ... }` guard.

## HTTP Pattern

Use the request/response envelope helpers instead of hand-building byte
headers when possible. `std.http.writeRequest` and
`std.http.writeJsonRequest` take a start line such as `"GET /health"` or
`"POST https://example.com/api"` and write into caller-owned storage. Use
`std.http.writeMethodRequest`, `std.http.writeGetRequest`, and the POST/PUT/PATCH
JSON request helpers when method and target are already separate values.

```zero
pub fn main() -> Void {
    var request_buf: [128]u8 = [0_u8; 128]
    let request: Maybe<Span<u8>> = std.http.writeJsonRequest(request_buf, "POST /users", "{\"id\":7}")
    expect request.has
}
```

For API-style handlers, parse the request envelope with route helpers such as
`std.http.requestIsGet`, `std.http.requestIsHead`,
`std.http.requestIsOptions`, `std.http.requestIsPost`,
`std.http.requestPathStartsWith`, `std.http.requestPathTailAfter`,
`std.http.requestRouteMatches`, `std.http.requestPathParam`,
`std.http.pathMatchesPattern`, `std.http.pathParam`,
`std.http.pathSegmentCount`, `std.http.pathSegment`,
`std.http.requestPathSegmentCount`, `std.http.requestPathSegment`,
`std.http.requestQueryValue`, `std.http.requestHeader`,
`std.http.requestContentLength`, `std.http.requestContentType`,
`std.http.requestAccepts`, `std.http.requestAcceptsJson`,
`std.http.requestBearerToken`, `std.http.requestCookie`,
`std.http.requestHasJsonContentType`, `std.http.requestJsonBodyWithin`, and
`std.http.requestJsonField`. Use `std.http.methodAllowed`,
`std.http.requestMethodAllowed`, and `std.http.requestRouteMethodAllowed` for
explicit handler dispatch across a comma-separated method allow list. Use path
segment helpers for resource routes such as `/users/7`; they borrow zero-based,
non-empty segments and ignore leading, trailing, or repeated `/`. Pattern routes
match literal segments, `:name` parameters, and a trailing `*` wildcard.
Prefer the status-specific JSON writers for common success responses and
`std.http.writeJsonError(response, status, code)` for conventional
`{"error":"code"}` failures. `writeJsonError` validates the code before writing
JSON, so agents do not need to hand-build simple error bodies. The full custom
body writers remain available:
`std.http.writeJsonOk`, `std.http.writeJsonCreated`,
`std.http.writeJsonBadRequest`, `std.http.writeJsonUnauthorized`,
`std.http.writeJsonForbidden`, `std.http.writeJsonNotFound`,
`std.http.writeJsonMethodNotAllowed`, `std.http.writeJsonConflict`,
`std.http.writeJsonUnprocessable`, `std.http.writeJsonTooManyRequests`, and
`std.http.writeJsonInternalServerError`. Use `std.http.writeNoContent` for
204 responses, `std.http.writeCorsPreflight` for `OPTIONS` preflight responses,
and `std.http.writeCorsJsonResponse` when a JSON response also needs
`access-control-allow-origin`. `writeCorsJsonResponse` takes a status-line
fragment such as `"200 OK"` or `"422 Unprocessable Entity"`. Use
`std.http.writeResponseWithHeader`, `std.http.writeJsonResponseWithHeader`, or
`std.http.writeJsonResponseWithCookie` when a response needs one explicit safe
header line or cookie value. Use the `WithHeaders` variants with
`std.http.headerBlockSafe` for newline-separated header blocks. Use
`std.http.writeRequestWithHeader`, `std.http.writeRequestWithHeaders`,
`std.http.writeJsonRequestWithHeader`, or
`std.http.writeJsonRequestWithHeaders` for outbound request envelopes with
explicit safe headers. These writers reject headers they manage themselves:
request writers reject `content-length` and `transfer-encoding`; JSON request
writers also reject `content-type`; response writers reject `content-length`,
`transfer-encoding`, and `connection`; JSON response writers also reject
`content-type`. Use `std.http.writeTextOk` or
`std.http.writeHtmlOk` for simple non-JSON responses such as health text,
`robots.txt`, or a small HTML page. Use `std.http.contentTypeForPath` and
`std.http.writeStaticResponse` for small static responses whose media type can
come from a path suffix. Use redirect helpers such as
`std.http.writeFound`, `std.http.writeSeeOther`,
`std.http.writeMovedPermanently`, or `std.http.writePermanentRedirect` instead
of hand-writing `Location` headers; they reject empty or control-character
location values before writing. Use
`std.http.responseStatus`, `std.http.responseStatusIs`,
`std.http.responseHeader`, `std.http.responseContentType`,
`std.http.responseRedirectLocation`, `std.http.responseBodyBytes`,
`std.http.responseBodyEquals`, and `std.http.responseMatches` to inspect a
local response envelope produced by the response writers. Use
`std.http.testRequest` and `std.http.testJsonRequest` to build synthetic request
envelopes for handler checks without opening a socket.

For a runnable local API server, define a same-module handler and call
`std.http.listen(world)` from `main`. The handler signature is
`fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>`.
The response span is fixed caller-owned storage; writers do not stream. The
listener accepts at most 65,536 bytes for the complete serialized HTTP
response: status line, headers, CRLF separators, `content-length` metadata, and
body. Bounded response writers return `Maybe.none` when input is invalid or
caller storage is insufficient. A listener `Maybe.none` is reported as HTTP
500 `{"error":"response_unavailable"}`; malformed output, handler process
failure, and capture overflow are reported as `response_invalid`,
`handler_failed`, and `response_too_large`. Listener errors include
`connection: close`. This fixed cap does not provide streaming or unbounded
responses.
When no port is passed, `std.http.listen(world)` starts at development port
`3000` and increments by one until it finds a free loopback port. It prints the
actual URL, such as `listening on http://127.0.0.1:3001`; use that printed port
for local HTTP requests. Do not assume `3000`, because another local server may
already be using it. When a port is explicit,
`std.http.listen(world, 3000_u16)` tries exactly that port and fails with a bind
diagnostic if it is occupied.

```zero
pub fn main(world: World) -> Void raises {
    check std.http.listen(world)
}

fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>> {
    if std.http.requestIsGet(request, "/ping") {
        return std.http.writeJsonOk(response, "{\"message\":\"pong\"}")
    }
    return std.http.writeJsonError(response, 404, "not_found")
}
```

```zero
pub fn main() -> Void {
    let request: Span<u8> = "POST /users?tenant=demo\ncontent-type: application/json\n\n{\"id\":7}"
    var response_buf: [192]u8 = [0_u8; 192]
    let body: Maybe<Span<u8>> = std.http.requestJsonBodyWithin(request, 64)
    let tenant: Maybe<Span<u8>> = std.http.requestQueryValue(request, "tenant")
    let resource: Maybe<Span<u8>> = std.http.requestPathSegment(request, 0)
    if std.http.requestIsPost(request, "/users") && resource.has && std.mem.eql(resource.value, "users") && tenant.has && body.has {
        let response: Maybe<Span<u8>> = std.http.writeJsonCreated(response_buf, "{\"created\":true}")
        expect response.has
    }
}
```

For browser-facing APIs, handle preflight and CORS in the response writer
instead of hand-assembling headers:

```zero
pub fn main() -> Void {
    let request: Span<u8> = "OPTIONS /users\naccess-control-request-method: POST\n\n"
    var response_buf: [256]u8 = [0_u8; 256]
    if std.http.requestIsOptions(request, "/users") {
        let response: Maybe<Span<u8>> = std.http.writeCorsPreflight(response_buf, "*", "GET, POST, OPTIONS", "content-type, authorization")
        expect response.has
    }
}
```

For authenticated APIs, use header-specific helpers rather than parsing
`authorization` or `cookie` by hand:

```zero
pub fn main() -> Void {
    let request: Span<u8> = "GET /me\nauthorization: Bearer token-123\ncookie: sid=abc; theme=dark\n\n"
    let token: Maybe<Span<u8>> = std.http.requestBearerToken(request)
    let session: Maybe<Span<u8>> = std.http.requestCookie(request, "sid")
    expect token.has && session.has
}
```

For hosted client calls, keep the network capability explicit and read response
bytes through `std.http.responseBody`. Use `std.http.headerBytes` when a
header value from `std.http.headerValue` must be borrowed as a span.

## Resource Pattern

Hosted file APIs can use explicit handles:

```zero
pub fn main(world: World) -> Void raises {
    let fs: Fs = std.fs.host()
    var read_buf: [32]u8 = [0_u8; 32]
    if std.fs.writeFile(fs, ".zero/out/log.txt", "hello\n") && std.fs.appendFile(fs, ".zero/out/log.txt", "again\n") && std.fs.readFileEquals(fs, ".zero/out/log.txt", read_buf, "hello\nagain\n") {
        check world.out.write("wrote\n")
    }
}
```

Owned resources are deterministic. Do not invent hidden heap, global logger, or ambient filesystem APIs.
