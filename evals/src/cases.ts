import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export type EvalCaseKind = "source" | "package";

export interface EvalRunCheck {
  name: string;
  args?: string[];
  expectedStdout: string;
  expectedStderr?: string;
}

export interface EvalServerCheck {
  name: string;
  path: string;
  expectedStdout: string;
  expectedStderr?: string;
}

// A near-miss source that the case's gates MUST reject. Used by `--fixture`
// to prove a case discriminates (rejects wrong answers), not only that the
// golden fixture passes. `expectGate` documents which gate is expected to
// catch it: "stdout"/"run" (run output), "pattern" (requiredSourcePatterns),
// or "check" (compiler check).
export interface NegativeFixture {
  label: string;
  source: string;
  expectGate: "stdout" | "pattern" | "check" | "run";
}

export interface EvalCase {
  id: string;
  title: string;
  prompt: string;
  kind?: EvalCaseKind;
  suites?: string[];
  fixtureSource?: string;
  fixtureProjectDir?: string;
  runArgs?: string[];
  runChecks?: EvalRunCheck[];
  serverChecks?: EvalServerCheck[];
  maxValidationDurationMs?: number;
  expectedStdout: string;
  expectedStderr?: string;
  requiredSourcePatterns: RegExp[];
  negativeFixtures?: NegativeFixture[];
}

interface RosettaChallenge {
  slug: string;
  title: string;
  prompt: string;
  expectedStdout: string;
  expectedStderr?: string;
  requiredSourcePatterns?: RegExp[];
}

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

const commonProgramPatterns = [/pub\s+fn\s+main/, /World/];
const packageProgramPatterns = [/zero\.toml|zero\.json/, /zero\.graph/];

function readRosettaFixture(slug: string) {
  return readFileSync(
    resolve(repoRoot, "benchmarks", "rosetta", `${slug}.0`),
    "utf8",
  );
}

function promptLines(lines: string[]) {
  return [
    ...lines,
    "Return only the Zero source code.",
  ].join("\n");
}

function rosettaCase(challenge: RosettaChallenge): EvalCase {
  const expectedStderr = challenge.expectedStderr ?? "";
  const outputPattern =
    expectedStderr.length > 0 && challenge.expectedStdout.length === 0
      ? /world\.err\.write/
      : /world\.out\.write/;
  return {
    id: `rosetta-${challenge.slug}`,
    title: `Rosetta: ${challenge.title}`,
    prompt: promptLines([
      `Write a single-file Zero program named ${challenge.slug}.0.`,
      `Solve the Rosetta Code task "${challenge.title}" for the deterministic checks below.`,
      challenge.prompt,
      `Print exactly ${JSON.stringify(challenge.expectedStdout)} to stdout.`,
      expectedStderr
        ? `Print exactly ${JSON.stringify(expectedStderr)} to stderr.`
        : "Do not write to stderr.",
    ]),
    fixtureSource: readRosettaFixture(challenge.slug),
    suites: ["rosetta"],
    expectedStdout: challenge.expectedStdout,
    expectedStderr,
    requiredSourcePatterns: [
      ...commonProgramPatterns,
      outputPattern,
      ...(challenge.requiredSourcePatterns ?? []),
    ],
  };
}

const baseEvalCases: EvalCase[] = [
  {
    id: "hello-world",
    title: "Hello world",
    prompt: promptLines([
      "Write a single-file Zero program named hello.0.",
      "It should print exactly `hello from zero` followed by a newline.",
    ]),
    fixtureSource: [
      "pub fn main(world: World) -> Void raises {",
      "    check world.out.write(\"hello from zero\\n\")",
      "}",
      "",
    ].join("\n"),
    suites: ["base"],
    expectedStdout: "hello from zero\n",
    requiredSourcePatterns: [
      /pub\s+fn\s+main/,
      /World/,
      /world\.out\.write/,
      /hello from zero/,
    ],
    negativeFixtures: [
      {
        label: "missing-trailing-newline",
        source: [
          "pub fn main(world: World) -> Void raises {",
          "    check world.out.write(\"hello from zero\")",
          "}",
          "",
        ].join("\n"),
        expectGate: "stdout",
      },
    ],
  },
  {
    id: "fibonacci",
    title: "Recursive Fibonacci",
    prompt: promptLines([
      "Write a single-file Zero program named fib.0.",
      "Define a helper `fib(n: u32) -> u32` that computes Fibonacci recursively.",
      "In `main`, call that helper to verify fib(0) through fib(10).",
      "Only when all results are correct, print exactly `fib sequence: 0 1 1 2 3 5 8 13 21 34 55` followed by a newline.",
    ]),
    fixtureSource: [
      "fn fib(n: u32) -> u32 {",
      "    if n <= 1 {",
      "        return n",
      "    }",
      "    return fib(n - 1) + fib(n - 2)",
      "}",
      "",
      "pub fn main(world: World) -> Void raises {",
      "    let ok: Bool = fib(0) == 0 && fib(1) == 1 && fib(2) == 1 && fib(3) == 2 && fib(4) == 3 && fib(5) == 5 && fib(6) == 8 && fib(7) == 13 && fib(8) == 21 && fib(9) == 34 && fib(10) == 55",
      "    if ok {",
      "        check world.out.write(\"fib sequence: 0 1 1 2 3 5 8 13 21 34 55\\n\")",
      "    }",
      "}",
      "",
    ].join("\n"),
    suites: ["base"],
    expectedStdout: "fib sequence: 0 1 1 2 3 5 8 13 21 34 55\n",
    requiredSourcePatterns: [
      /fn\s+fib\s*\(\s*n\s*:\s*u32\s*\)\s*->\s*u32/,
      /fib\s*\(\s*n\s*-\s*1\s*\)\s*\+\s*fib\s*\(\s*n\s*-\s*2\s*\)/,
      /fib\s*\(\s*0(?:_u32)?\s*\)\s*==\s*0/,
      /fib\s*\(\s*1(?:_u32)?\s*\)\s*==\s*1/,
      /fib\s*\(\s*2(?:_u32)?\s*\)\s*==\s*1/,
      /fib\s*\(\s*3(?:_u32)?\s*\)\s*==\s*2/,
      /fib\s*\(\s*4(?:_u32)?\s*\)\s*==\s*3/,
      /fib\s*\(\s*5(?:_u32)?\s*\)\s*==\s*5/,
      /fib\s*\(\s*6(?:_u32)?\s*\)\s*==\s*8/,
      /fib\s*\(\s*7(?:_u32)?\s*\)\s*==\s*13/,
      /fib\s*\(\s*8(?:_u32)?\s*\)\s*==\s*21/,
      /fib\s*\(\s*9(?:_u32)?\s*\)\s*==\s*34/,
      /fib\s*\(\s*10(?:_u32)?\s*\)\s*==\s*55/,
      /pub\s+fn\s+main/,
      /World/,
      /world\.out\.write/,
      /fib sequence: 0 1 1 2 3 5 8 13 21 34 55/,
    ],
    negativeFixtures: [
      {
        // Drops the `fib(10) == 55` term from the verification guard. The run
        // still prints the same line, so only the per-value requiredSourcePattern
        // can catch it — proving those asserts are load-bearing.
        label: "drops-fib10-verification",
        source: [
          "fn fib(n: u32) -> u32 {",
          "    if n <= 1 {",
          "        return n",
          "    }",
          "    return fib(n - 1) + fib(n - 2)",
          "}",
          "",
          "pub fn main(world: World) -> Void raises {",
          "    let ok: Bool = fib(0) == 0 && fib(1) == 1 && fib(2) == 1 && fib(3) == 2 && fib(4) == 3 && fib(5) == 5 && fib(6) == 8 && fib(7) == 13 && fib(8) == 21 && fib(9) == 34",
          "    if ok {",
          "        check world.out.write(\"fib sequence: 0 1 1 2 3 5 8 13 21 34 55\\n\")",
          "    }",
          "}",
          "",
        ].join("\n"),
        expectGate: "pattern",
      },
    ],
  },
];

const scaleCliUsage =
  "usage: zero run . -- <add|subtract|multiply|help> <x> <y>\n";
const scaleOpsUsage =
  "usage: zero run . -- <summary|overdue|csv|help>\n";
const scaleOpsSummary =
  "pipeline: $215000\nweighted: $104000\nopen deals: 3\noverdue activities: 2\n";
const scaleOpsOverdue =
  "overdue activities: 2\n- Grace Hopper: follow up proposal\n- Alan Turing: send renewal note\n";
const scaleOpsCsv =
  "account,contact,stage,amount\nAcme,Grace Hopper,proposal,120000\nGlobex,Ada Lovelace,qualified,65000\nInitech,Alan Turing,discovery,30000\n";
const scalePingResponse =
  "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 18\n\n{\"message\":\"pong\"}";
const scaleHealthResponse =
  "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 11\n\n{\"ok\":true}";
const scaleMissingResponse =
  "HTTP/1.1 404 Not Found\ncontent-type: application/json\ncontent-length: 21\n\n{\"error\":\"not_found\"}";

const agentScaleEvalCases: EvalCase[] = [
  {
    id: "scale-multi-command-cli",
    title: "Agent scale: multi-command arithmetic CLI",
    suites: ["agent-scale"],
    prompt: promptLines([
      "Build a graph-first Zerolang arithmetic CLI.",
      "It should support `add`, `subtract`, `multiply`, and `help` commands.",
      "The arithmetic commands take two non-negative integer CLI arguments and print one decimal result plus a newline.",
      "The help command prints the exact usage line for the CLI.",
      "The evaluator will run several command combinations, not just one happy path.",
    ]),
    fixtureSource: [
      "fn add(x: u32, y: u32) -> u32 {",
      "    return x + y",
      "}",
      "",
      "fn subtract(x: u32, y: u32) -> u32 {",
      "    return x - y",
      "}",
      "",
      "fn multiply(x: u32, y: u32) -> u32 {",
      "    return x * y",
      "}",
      "",
      "pub fn main(world: World) -> Void raises {",
      "    if std.cli.argEquals(1, \"help\") {",
      `        check world.out.write(${JSON.stringify(scaleCliUsage)})`,
      "        return",
      "    }",
      "    let x: Maybe<u32> = std.args.parseU32(2)",
      "    let y: Maybe<u32> = std.args.parseU32(3)",
      "    if !x.has || !y.has {",
      `        check world.out.write(${JSON.stringify(scaleCliUsage)})`,
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"add\") {",
      "        let result: u32 = add(x.value, y.value)",
      "        var out: [10]u8 = [0_u8; 10]",
      "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
      "        if text.has {",
      "            check world.out.write(text.value)",
      "            check world.out.write(\"\\n\")",
      "        }",
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"subtract\") {",
      "        let result: u32 = subtract(x.value, y.value)",
      "        var out: [10]u8 = [0_u8; 10]",
      "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
      "        if text.has {",
      "            check world.out.write(text.value)",
      "            check world.out.write(\"\\n\")",
      "        }",
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"multiply\") {",
      "        let result: u32 = multiply(x.value, y.value)",
      "        var out: [10]u8 = [0_u8; 10]",
      "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
      "        if text.has {",
      "            check world.out.write(text.value)",
      "            check world.out.write(\"\\n\")",
      "        }",
      "        return",
      "    }",
      `    check world.out.write(${JSON.stringify(scaleCliUsage)})`,
      "}",
      "",
    ].join("\n"),
    runArgs: ["help"],
    expectedStdout: scaleCliUsage,
    maxValidationDurationMs: 10_000,
    runChecks: [
      {
        name: "help",
        args: ["help"],
        expectedStdout: scaleCliUsage,
      },
      {
        name: "add",
        args: ["add", "40", "2"],
        expectedStdout: "42\n",
      },
      {
        name: "subtract",
        args: ["subtract", "40", "2"],
        expectedStdout: "38\n",
      },
      {
        name: "multiply",
        args: ["multiply", "6", "7"],
        expectedStdout: "42\n",
      },
      {
        name: "bad-args",
        args: ["add", "nope", "2"],
        expectedStdout: scaleCliUsage,
      },
    ],
    requiredSourcePatterns: [
      ...commonProgramPatterns,
      /std\.cli\.argEquals/,
      /std\.args\.parseU32/,
      /std\.fmt\.u32/,
      /fn\s+add/,
      /fn\s+subtract/,
      /fn\s+multiply/,
      /usage: zero run \. -- <add\|subtract\|multiply\|help> <x> <y>/,
    ],
    negativeFixtures: [
      {
        // `multiply` returns x - y instead of x * y; help and the other
        // commands are correct. Passes the help happy-path and every source
        // pattern (fn multiply still exists), so only the multi-command
        // runChecks can catch it — proving coverage isn't met by one route.
        label: "multiply-returns-subtraction",
        source: [
          "fn add(x: u32, y: u32) -> u32 {",
          "    return x + y",
          "}",
          "",
          "fn subtract(x: u32, y: u32) -> u32 {",
          "    return x - y",
          "}",
          "",
          "fn multiply(x: u32, y: u32) -> u32 {",
          "    return x - y",
          "}",
          "",
          "pub fn main(world: World) -> Void raises {",
          "    if std.cli.argEquals(1, \"help\") {",
          `        check world.out.write(${JSON.stringify(scaleCliUsage)})`,
          "        return",
          "    }",
          "    let x: Maybe<u32> = std.args.parseU32(2)",
          "    let y: Maybe<u32> = std.args.parseU32(3)",
          "    if !x.has || !y.has {",
          `        check world.out.write(${JSON.stringify(scaleCliUsage)})`,
          "        return",
          "    }",
          "    if std.cli.argEquals(1, \"add\") {",
          "        let result: u32 = add(x.value, y.value)",
          "        var out: [10]u8 = [0_u8; 10]",
          "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
          "        if text.has {",
          "            check world.out.write(text.value)",
          "            check world.out.write(\"\\n\")",
          "        }",
          "        return",
          "    }",
          "    if std.cli.argEquals(1, \"subtract\") {",
          "        let result: u32 = subtract(x.value, y.value)",
          "        var out: [10]u8 = [0_u8; 10]",
          "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
          "        if text.has {",
          "            check world.out.write(text.value)",
          "            check world.out.write(\"\\n\")",
          "        }",
          "        return",
          "    }",
          "    if std.cli.argEquals(1, \"multiply\") {",
          "        let result: u32 = multiply(x.value, y.value)",
          "        var out: [10]u8 = [0_u8; 10]",
          "        let text: Maybe<Span<u8>> = std.fmt.u32(out, result)",
          "        if text.has {",
          "            check world.out.write(text.value)",
          "            check world.out.write(\"\\n\")",
          "        }",
          "        return",
          "    }",
          `    check world.out.write(${JSON.stringify(scaleCliUsage)})`,
          "}",
          "",
        ].join("\n"),
        expectGate: "run",
      },
    ],
  },
  {
    id: "scale-ops-report-script",
    title: "Agent scale: operations report script",
    suites: ["agent-scale"],
    prompt: promptLines([
      "Build a graph-first Zerolang operations reporting script.",
      "It should support `summary`, `overdue`, `csv`, and `help` commands.",
      "The script should model a tiny CRM pipeline with deterministic in-program data.",
      "The summary command prints pipeline total, weighted pipeline, open deal count, and overdue activity count.",
      "The overdue command prints the overdue follow-up list.",
      "The csv command prints a deterministic CSV export.",
      "The evaluator will run each command and compare exact stdout.",
    ]),
    fixtureSource: [
      "fn pipelineTotal() -> u32 {",
      "    return 120000 + 65000 + 30000",
      "}",
      "",
      "fn weightedPipeline() -> u32 {",
      "    return 72000 + 26000 + 6000",
      "}",
      "",
      "fn openDealCount() -> u32 {",
      "    return 3",
      "}",
      "",
      "fn overdueActivityCount() -> u32 {",
      "    return 2",
      "}",
      "",
      "pub fn main(world: World) -> Void raises {",
      "    if std.cli.argEquals(1, \"summary\") {",
      "        var pipelineOut: [10]u8 = [0_u8; 10]",
      "        var weightedOut: [10]u8 = [0_u8; 10]",
      "        var openDealsOut: [10]u8 = [0_u8; 10]",
      "        var overdueOut: [10]u8 = [0_u8; 10]",
      "        let pipelineText: Maybe<Span<u8>> = std.fmt.u32(pipelineOut, pipelineTotal())",
      "        let weightedText: Maybe<Span<u8>> = std.fmt.u32(weightedOut, weightedPipeline())",
      "        let openDealsText: Maybe<Span<u8>> = std.fmt.u32(openDealsOut, openDealCount())",
      "        let overdueText: Maybe<Span<u8>> = std.fmt.u32(overdueOut, overdueActivityCount())",
      "        if pipelineText.has && weightedText.has && openDealsText.has && overdueText.has {",
      "            check world.out.write(\"pipeline: $\")",
      "            check world.out.write(pipelineText.value)",
      "            check world.out.write(\"\\nweighted: $\")",
      "            check world.out.write(weightedText.value)",
      "            check world.out.write(\"\\nopen deals: \")",
      "            check world.out.write(openDealsText.value)",
      "            check world.out.write(\"\\noverdue activities: \")",
      "            check world.out.write(overdueText.value)",
      "            check world.out.write(\"\\n\")",
      "        }",
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"overdue\") {",
      "        check world.out.write(\"overdue activities: 2\\n\")",
      "        check world.out.write(\"- Grace Hopper: follow up proposal\\n\")",
      "        check world.out.write(\"- Alan Turing: send renewal note\\n\")",
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"csv\") {",
      "        check world.out.write(\"account,contact,stage,amount\\n\")",
      "        check world.out.write(\"Acme,Grace Hopper,proposal,120000\\n\")",
      "        check world.out.write(\"Globex,Ada Lovelace,qualified,65000\\n\")",
      "        check world.out.write(\"Initech,Alan Turing,discovery,30000\\n\")",
      "        return",
      "    }",
      "    if std.cli.argEquals(1, \"help\") {",
      `        check world.out.write(${JSON.stringify(scaleOpsUsage)})`,
      "        return",
      "    }",
      `    check world.out.write(${JSON.stringify(scaleOpsUsage)})`,
      "}",
      "",
    ].join("\n"),
    runArgs: ["summary"],
    expectedStdout: scaleOpsSummary,
    maxValidationDurationMs: 15_000,
    runChecks: [
      {
        name: "summary",
        args: ["summary"],
        expectedStdout: scaleOpsSummary,
      },
      {
        name: "overdue",
        args: ["overdue"],
        expectedStdout: scaleOpsOverdue,
      },
      {
        name: "csv",
        args: ["csv"],
        expectedStdout: scaleOpsCsv,
      },
      {
        name: "help",
        args: ["help"],
        expectedStdout: scaleOpsUsage,
      },
    ],
    requiredSourcePatterns: [
      ...commonProgramPatterns,
      /std\.cli\.argEquals/,
      /std\.fmt\.u32/,
      /fn\s+pipelineTotal/,
      /fn\s+weightedPipeline/,
      /fn\s+openDealCount/,
      /fn\s+overdueActivityCount/,
      /Grace Hopper/,
      /Ada Lovelace/,
      /Alan Turing/,
      /usage: zero run \. -- <summary\|overdue\|csv\|help>/,
    ],
  },
  {
    id: "scale-ping-pong-web-server",
    title: "Agent scale: curlable ping/pong web server package",
    kind: "package",
    suites: ["agent-scale"],
    prompt: [
      "Build a graph-first Zerolang ping/pong web server package.",
      "Create the package in the candidate package root provided by the evaluator.",
      "Use std.http.listen(world) without an explicit port so Zero can auto-select a free local development port.",
      "Implement a same-module handle(request, response) function.",
      "GET /ping should return JSON {\"message\":\"pong\"}.",
      "GET /health should return JSON {\"ok\":true}.",
      "Unknown routes should return JSON {\"error\":\"not_found\"} with a 404 response.",
    ].join("\n"),
    fixtureProjectDir: "examples/ping-pong-api",
    expectedStdout: scalePingResponse,
    maxValidationDurationMs: 45_000,
    serverChecks: [
      {
        name: "ping",
        path: "/ping",
        expectedStdout: scalePingResponse,
      },
      {
        name: "health",
        path: "/health",
        expectedStdout: scaleHealthResponse,
      },
      {
        name: "missing",
        path: "/missing",
        expectedStdout: scaleMissingResponse,
      },
    ],
    requiredSourcePatterns: [
      ...packageProgramPatterns,
      /std\.http\.listen\s*\(\s*world\s*\)/,
      /fn\s+handle/,
      /std\.http\.requestIsGet/,
      /std\.http\.writeJsonOk/,
      /std\.http\.writeJsonNotFound/,
      /\/ping/,
      /\/health/,
      /not_found/,
    ],
  },
  {
    id: "scale-crm-http-api",
    title: "Agent scale: CRM HTTP API package",
    kind: "package",
    suites: ["agent-scale"],
    prompt: [
      "Build a graph-first Zerolang CRM HTTP request-envelope API package.",
      "Create the package in the candidate package root provided by the evaluator.",
      "It should expose at least ten route branches across health, accounts, contacts, deals, activities, and search.",
      "Cover CRUD-style list, create, read, update, and delete flows where the current HTTP helpers support them.",
      "Use deterministic in-memory responses so the evaluator can run request envelopes without external services.",
    ].join("\n"),
    fixtureProjectDir: "examples/crm-api",
    runArgs: ["GET /health\n\n"],
    expectedStdout:
      "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 27\n\n{\"ok\":true,\"service\":\"crm\"}",
    maxValidationDurationMs: 45_000,
    runChecks: [
      {
        name: "health",
        args: ["GET /health\n\n"],
        expectedStdout:
          "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 27\n\n{\"ok\":true,\"service\":\"crm\"}",
      },
      {
        name: "contact-read",
        args: ["GET /crm/contacts/7\n\n"],
        expectedStdout:
          "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 85\n\n{\"contact\":{\"id\":7,\"name\":\"Grace Hopper\",\"email\":\"grace@example.com\",\"account_id\":1}}",
      },
      {
        name: "deal-read",
        args: ["GET /crm/deals/42\n\n"],
        expectedStdout:
          "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 72\n\n{\"deal\":{\"id\":42,\"name\":\"Expansion\",\"stage\":\"proposal\",\"amount\":120000}}",
      },
      {
        name: "search",
        args: ["GET /crm/search\n\n"],
        expectedStdout:
          "HTTP/1.1 200 OK\ncontent-type: application/json\ncontent-length: 98\n\n{\"results\":[{\"type\":\"account\",\"id\":1,\"label\":\"Acme\"},{\"type\":\"deal\",\"id\":42,\"label\":\"Expansion\"}]}",
      },
      {
        name: "not-found",
        args: ["GET /crm/missing\n\n"],
        expectedStdout:
          "HTTP/1.1 404 Not Found\ncontent-type: application/json\ncontent-length: 38\n\n{\"error\":\"not_found\",\"resource\":\"crm\"}",
      },
    ],
    requiredSourcePatterns: [
      ...packageProgramPatterns,
      /handleCrm|fn\s+handle/,
      /std\.http\.requestIsGet/,
      /std\.http\.requestIsPost/,
      /\/crm\/accounts/,
      /\/crm\/contacts/,
      /\/crm\/deals/,
      /\/crm\/activities/,
      /\/crm\/search/,
      /badRequest|methodNotAllowed|notFound/,
    ],
  },
];

const rosettaChallenges: RosettaChallenge[] = [
  {
    slug: "100-doors",
    title: "100 doors",
    prompt: "Toggle doors 1 through 100 over 100 passes. Verify that exactly the perfect-square numbered doors are open at the end before printing success.",
    expectedStdout: "100 doors ok\n",
    requiredSourcePatterns: [/doors/, /while/, /100/],
  },
  {
    slug: "abc-problem",
    title: "ABC problem",
    prompt: "Use the full Rosetta block set `BO XK DQ CP NA GT RE TG QD FS JW HU VI AN OB ER FS LY PC ZM`. Define `canSpell(word: Span<u8>) -> Bool` and verify A, BARK, BOOK, TREAT, COMMON, SQUAD, and CONFUSE.",
    expectedStdout: "abc blocks ok\n",
    requiredSourcePatterns: [/fn\s+canSpell/, /BARK/, /CONFUSE/],
  },
  {
    slug: "ackermann-function",
    title: "Ackermann function",
    prompt: "Define an Ackermann helper for the small Rosetta values A(0,4), A(1,4), A(2,4), and A(3,2), then verify they are 5, 6, 11, and 29.",
    expectedStdout: "ackermann ok\n",
    requiredSourcePatterns: [/ack/i, /0,\s*4/, /3,\s*2/],
  },
  {
    slug: "arithmetic-integer",
    title: "Integer arithmetic",
    prompt: "Show integer addition, subtraction, multiplication, and remainder with 21 and 6. Verify the results are 27, 15, 126, and 3.",
    expectedStdout: "integer arithmetic ok\n",
    requiredSourcePatterns: [/\+/, /-/, /\*/, /%/],
  },
  {
    slug: "array-concatenation",
    title: "Array concatenation",
    prompt: "Concatenate `[1, 2]` and `[3, 4, 5]` into one five-item array. Verify the length and boundary values.",
    expectedStdout: "array concatenation ok\n",
    requiredSourcePatterns: [/std\.collections\.append|while/, /\[5\]i32/],
  },
  {
    slug: "array-length",
    title: "Array length",
    prompt: "Create a four-item integer array and verify its length is 4.",
    expectedStdout: "array length ok\n",
    requiredSourcePatterns: [/std\.mem\.len/, /\[4\]i32/],
  },
  {
    slug: "arrays",
    title: "Arrays",
    prompt: "Create a fixed-size integer array, add the values 3, 1, 4, 1, and verify the resulting length and indexed values.",
    expectedStdout: "arrays ok\n",
    requiredSourcePatterns: [/std\.collections\.push|while/, /\[4\]i32/],
  },
  {
    slug: "assertions",
    title: "Assertions",
    prompt: "Evaluate an assertion-style boolean check that 20 + 22 equals 42 before printing success.",
    expectedStdout: "assertions ok\n",
    requiredSourcePatterns: [/Bool/, /20\s*\+\s*22\s*==\s*42/],
  },
  {
    slug: "babbage-problem",
    title: "Babbage problem",
    prompt: "Find the smallest positive integer whose square ends with 269696. Define a helper and verify the answer is 25264.",
    expectedStdout: "babbage ok\n",
    requiredSourcePatterns: [/fn\s+babbage/, /269696/, /25264/],
  },
  {
    slug: "balanced-brackets",
    title: "Balanced brackets",
    prompt: "Define `balanced(text: Span<u8>) -> Bool` for square brackets. Verify `[[][]]` and `[][]` are balanced, while `[]][[]` and `][` are not.",
    expectedStdout: "balanced brackets ok\n",
    requiredSourcePatterns: [/fn\s+balanced/, /depth/],
  },
  {
    slug: "boolean-values",
    title: "Boolean values",
    prompt: "Demonstrate boolean true, false, conjunction, and negation by proving `true && !false`.",
    expectedStdout: "boolean values ok\n",
    requiredSourcePatterns: [/Bool/, /true\s*&&\s*!false/],
  },
  {
    slug: "caesar-cipher",
    title: "Caesar cipher",
    prompt: "Implement a Caesar shift helper for ASCII letters. Shift `Attack at Z` by 3 and verify selected output bytes spell the expected cipher text.",
    expectedStdout: "caesar cipher ok\n",
    requiredSourcePatterns: [/fn\s+shift/, /Attack at Z/, /%\s*26/],
  },
  {
    slug: "character-codes",
    title: "Character codes",
    prompt: "Read the byte code for the character `A` and verify it is 65.",
    expectedStdout: "character codes ok\n",
    requiredSourcePatterns: [/"A"\[0\]/, /65/],
  },
  {
    slug: "comments",
    title: "Comments",
    prompt: "Include at least one Zero comment and verify a simple calculation before printing success.",
    expectedStdout: "comments ok\n",
    requiredSourcePatterns: [/\/\//, /1\s*\+\s*1/],
  },
  {
    slug: "copy-a-string",
    title: "Copy a string",
    prompt: "Copy the string `zero` into a byte buffer and verify the copied span equals `zero`.",
    expectedStdout: "copy string ok\n",
    requiredSourcePatterns: [/std\.str\.copy/, /std\.mem\.eql/],
  },
  {
    slug: "count-occurrences-of-a-substring",
    title: "Count occurrences of a substring",
    prompt: "Count non-overlapping occurrences of `an` in `banana` and verify the count is 2.",
    expectedStdout: "substring occurrences ok\n",
    requiredSourcePatterns: [/std\.str\.count|while/, /banana/, /an/],
  },
  {
    slug: "crc-32",
    title: "CRC-32",
    prompt: "Compute CRC-32 for `The quick brown fox jumps over the lazy dog` and verify the value is `1095738169_u32`.",
    expectedStdout: "crc ok\n",
    requiredSourcePatterns: [/std\.codec\.crc32/, /1095738169_u32/],
  },
  {
    slug: "cusip",
    title: "CUSIP",
    prompt: "Implement the CUSIP check digit calculation for `03783310` and verify the check digit is 0.",
    expectedStdout: "cusip ok\n",
    requiredSourcePatterns: [/fn\s+checkDigit/, /03783310/],
  },
  {
    slug: "department-numbers",
    title: "Department numbers",
    prompt: "Search police, sanitation, and fire department numbers from 1 through 7. They must be distinct, sum to 12, and multiply to 48. Verify `(2, 4, 6)` and count all ordered solutions.",
    expectedStdout: "departments ok\n",
    requiredSourcePatterns: [/fn\s+valid/, /validOrderCount/, /48/],
  },
  {
    slug: "determine-if-a-string-is-numeric",
    title: "Determine if a string is numeric",
    prompt: "Parse `12345` as a usize and reject `12a`. Only print success if both checks behave correctly.",
    expectedStdout: "numeric string ok\n",
    requiredSourcePatterns: [/std\.parse\.parseUsize/, /12345/, /12a/],
  },
  {
    slug: "ethiopian-multiplication",
    title: "Ethiopian multiplication",
    prompt: "Implement Ethiopian multiplication by halving and doubling. Verify `17 * 34` produces 578.",
    expectedStdout: "ethiopian multiplication ok\n",
    requiredSourcePatterns: [/fn\s+eth/, /\/\s*2/, /\*\s*2/],
  },
  {
    slug: "factorial",
    title: "Factorial",
    prompt: "Compute 6 factorial and verify the result is 720.",
    expectedStdout: "factorial ok\n",
    requiredSourcePatterns: [/factorial|while/, /720/],
  },
  {
    slug: "fibonacci-sequence",
    title: "Fibonacci sequence",
    prompt: "Implement iterative Fibonacci. Verify fib(0), fib(1), fib(2), fib(10), and the sum fib(0) through fib(10) equals 143.",
    expectedStdout: "fibonacci ok\n",
    requiredSourcePatterns: [/fn\s+fib/, /fibSum/, /143/],
  },
  {
    slug: "fizzbuzz",
    title: "FizzBuzz",
    prompt: "Classify numbers from 1 through 100 as plain, fizz, buzz, or fizzbuzz. Verify examples 1, 3, 5, 15, 30 and the class counts 53, 27, 14, and 6.",
    expectedStdout: "fizzbuzz ok\n",
    requiredSourcePatterns: [/fn\s+fizzCode/, /countCode/, /15/],
  },
  {
    slug: "function-definition",
    title: "Function definition",
    prompt: "Define `square(value: i32) -> i32` and verify square(9) is 81.",
    expectedStdout: "function definition ok\n",
    requiredSourcePatterns: [/fn\s+square\s*\(/, /value\s*\*\s*value/],
  },
  {
    slug: "generate-lower-case-ascii-alphabet",
    title: "Generate lower-case ASCII alphabet",
    prompt: "Generate the lowercase ASCII alphabet bytes into a 26-byte array and verify the first and last bytes are `a` and `z`.",
    expectedStdout: "ascii alphabet ok\n",
    requiredSourcePatterns: [/\[26\]u8/, /97/, /122/],
  },
  {
    slug: "greatest-common-divisor",
    title: "Greatest common divisor",
    prompt: "Compute the greatest common divisor of 84 and 30 and verify it is 6.",
    expectedStdout: "gcd ok\n",
    requiredSourcePatterns: [/gcd|while/, /84/, /30/],
  },
  {
    slug: "gray-code",
    title: "Gray code",
    prompt: "Implement binary-to-Gray-code conversion and verify the first eight values are 0, 1, 3, 2, 6, 7, 5, 4.",
    expectedStdout: "gray code ok\n",
    requiredSourcePatterns: [/fn\s+gray/, /expected/, /0,\s*1,\s*3,\s*2/],
  },
  {
    slug: "hello-world-newline-omission",
    title: "Hello world newline omission",
    prompt: "Print `Hello, world!` to stdout with no trailing newline.",
    expectedStdout: "Hello, world!",
    requiredSourcePatterns: [/Hello, world!/, /world\.out\.write/],
  },
  {
    slug: "hello-world-standard-error",
    title: "Hello world standard error",
    prompt: "Print `Hello, stderr!` followed by a newline to stderr and write nothing to stdout.",
    expectedStdout: "",
    expectedStderr: "Hello, stderr!\n",
    requiredSourcePatterns: [/Hello, stderr!/, /world\.err\.write/],
  },
  {
    slug: "hello-world-text",
    title: "Hello world text",
    prompt: "Print `Hello, world!` followed by a newline to stdout.",
    expectedStdout: "Hello, world!\n",
    requiredSourcePatterns: [/Hello, world!/, /world\.out\.write/],
  },
  {
    slug: "leap-year",
    title: "Leap year",
    prompt: "Define a leap-year helper. Verify 2000 is a leap year and 1900 is not.",
    expectedStdout: "leap year ok\n",
    requiredSourcePatterns: [/fn\s+leap/, /400/, /100/, /4/],
  },
  {
    slug: "loops-downward-for",
    title: "Loops downward for",
    prompt: "Use a downward loop from 5 to 1 and verify the sum is 15.",
    expectedStdout: "downward loop ok\n",
    requiredSourcePatterns: [/while/, /i\s*-\s*1/, /15/],
  },
  {
    slug: "loops-for",
    title: "Loops for",
    prompt: "Use a loop to sum integers 0 through 9 and verify the sum is 45.",
    expectedStdout: "for loop ok\n",
    requiredSourcePatterns: [/while/, /45/],
  },
  {
    slug: "loops-for-with-a-specified-step",
    title: "Loops with a specified step",
    prompt: "Use a loop with step 2 to sum 0, 2, 4, 6, 8, and 10. Verify the sum is 30.",
    expectedStdout: "step loop ok\n",
    requiredSourcePatterns: [/while/, /\+\s*2/, /30/],
  },
  {
    slug: "loops-foreach",
    title: "Loops foreach",
    prompt: "Iterate over the array `[1, 2, 3, 4]` and verify the sum is 10.",
    expectedStdout: "foreach loop ok\n",
    requiredSourcePatterns: [/\[4\]u32/, /while/, /10/],
  },
  {
    slug: "loops-nested",
    title: "Nested loops",
    prompt: "Use nested loops with 3 outer iterations and 4 inner iterations. Verify the total count is 12.",
    expectedStdout: "nested loops ok\n",
    requiredSourcePatterns: [/outer/, /inner/, /12/],
  },
  {
    slug: "loops-while",
    title: "Loops while",
    prompt: "Use a while loop to increment a counter from 0 to 4 and verify the final value.",
    expectedStdout: "while loop ok\n",
    requiredSourcePatterns: [/while/, /<\s*4/],
  },
  {
    slug: "modular-arithmetic",
    title: "Modular arithmetic",
    prompt: "Compute modular exponentiation for 4^13 mod 497 and verify the result is 445.",
    expectedStdout: "modular arithmetic ok\n",
    requiredSourcePatterns: [/modPow|while/, /497/, /445/],
  },
  {
    slug: "modular-inverse",
    title: "Modular inverse",
    prompt: "Implement modular inverse with the extended Euclidean algorithm. Verify inverse(3, 11) is 4.",
    expectedStdout: "modular inverse ok\n",
    requiredSourcePatterns: [/fn\s+inverse/, /while/, /3,\s*11/],
  },
  {
    slug: "mutual-recursion",
    title: "Mutual recursion",
    prompt: "Define mutually recursive `even` and `odd` helpers. Verify even(10) and odd(9).",
    expectedStdout: "mutual recursion ok\n",
    requiredSourcePatterns: [/fn\s+even/, /fn\s+odd/, /odd\s*\(\s*n\s*-\s*1\s*\)/],
  },
  {
    slug: "parametric-polymorphism",
    title: "Parametric polymorphism",
    prompt: "Define a generic identity helper and use it with `i32` to verify the value 42.",
    expectedStdout: "parametric polymorphism ok\n",
    requiredSourcePatterns: [/fn\s+id\s*<T:\s*Type>/, /id\s*<i32>/],
  },
  {
    slug: "repeat-a-string",
    title: "Repeat a string",
    prompt: "Repeat `ha` three times into a buffer and verify the result is `hahaha`.",
    expectedStdout: "repeat string ok\n",
    requiredSourcePatterns: [/std\.str\.repeat|while/, /hahaha/],
  },
  {
    slug: "reverse-a-string",
    title: "Reverse a string",
    prompt: "Reverse `drawer` and verify the result is `reward`.",
    expectedStdout: "reverse string ok\n",
    requiredSourcePatterns: [/std\.str\.reverse|while/, /drawer/, /reward/],
  },
  {
    slug: "rot-13",
    title: "ROT-13",
    prompt: "Implement ROT-13 for ASCII letters. Encode `Hello, zero!`, decode it again, and verify selected encoded bytes and the round trip.",
    expectedStdout: "rot13 ok\n",
    requiredSourcePatterns: [/fn\s+rot13/, /13/, /Hello, zero!/],
  },
  {
    slug: "sieve-of-eratosthenes",
    title: "Sieve of Eratosthenes",
    prompt: "Run a Sieve of Eratosthenes up to 30. Verify 2 and 29 are prime and 30 is not.",
    expectedStdout: "sieve ok\n",
    requiredSourcePatterns: [/prime/, /while/, /29/],
  },
  {
    slug: "string-case",
    title: "String case",
    prompt: "Convert `Zero` to uppercase and lowercase ASCII. Verify the results are `ZERO` and `zero`.",
    expectedStdout: "string case ok\n",
    requiredSourcePatterns: [/toUpperAscii|while/, /toLowerAscii|while/, /ZERO/],
  },
  {
    slug: "string-concatenation",
    title: "String concatenation",
    prompt: "Concatenate `zero` and `lang` into a buffer and verify the result is `zerolang`.",
    expectedStdout: "string concatenation ok\n",
    requiredSourcePatterns: [/std\.str\.concat|while/, /zerolang/],
  },
  {
    slug: "string-length",
    title: "String length",
    prompt: "Compute the byte length of `zero` and verify it is 4.",
    expectedStdout: "string length ok\n",
    requiredSourcePatterns: [/std\.mem\.len/, /zero/],
  },
  {
    slug: "sum-and-product-of-an-array",
    title: "Sum and product of an array",
    prompt: "Compute the sum and product of `[1, 2, 3, 4]`. Verify the sum is 10 and the product is 24.",
    expectedStdout: "sum product ok\n",
    requiredSourcePatterns: [/sum/, /product/, /24/],
  },
  {
    slug: "sum-multiples-of-3-and-5",
    title: "Sum multiples of 3 and 5",
    prompt: "Sum all numbers below 1000 that are multiples of 3 or 5. Verify the result is 233168.",
    expectedStdout: "sum multiples ok\n",
    requiredSourcePatterns: [/1000/, /233168/, /%\s*3/, /%\s*5/],
  },
  {
    slug: "tokenize-a-string",
    title: "Tokenize a string",
    prompt: "Tokenize `  zero text syntax`. Verify the first token is `zero` and the word count for `zero text syntax` is 3.",
    expectedStdout: "tokenize ok\n",
    requiredSourcePatterns: [/tokenAscii|while/, /wordCountAscii|while/, /zero text syntax/],
  },
  {
    slug: "zero-to-the-zero-power",
    title: "Zero to the zero power",
    prompt: "Evaluate 0^0 using integer power semantics and verify the result is 1.",
    expectedStdout: "zero power ok\n",
    requiredSourcePatterns: [/powU32|0\s*\*\*\s*0|0,\s*0/, /1/],
  },
];

export const evalCases: EvalCase[] = [
  ...baseEvalCases,
  ...rosettaChallenges.map(rosettaCase),
  ...agentScaleEvalCases,
];

export function findEvalCase(id: string): EvalCase | undefined {
  return evalCases.find((evalCase) => evalCase.id === id);
}

export function evalSuiteIds(): string[] {
  return uniqueOrdered(evalCases.flatMap((evalCase) => evalCase.suites ?? []));
}

export function findEvalSuite(suiteId: string): EvalCase[] {
  return evalCases.filter((evalCase) => evalCase.suites?.includes(suiteId));
}

function uniqueOrdered(values: string[]) {
  return values.filter((value, index) => values.indexOf(value) === index);
}
