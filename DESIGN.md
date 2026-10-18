# ClangSOLV design, intent, philosophy and implementation plan

## Intent and architectural choice

Bring JSOLV's unobtrusive annotation experience to C++: keep ordinary domain
values and signatures, express application-owned facts, and let the checker
propagate evidence through ordinary assignments and control flow. Retain the
portable CSOLV library alongside this frontend. They are complementary choices,
not interchangeable type or ABI representations.

ClangSOLV links real Clang LibTooling, AST and analysis libraries. It is a
separate executable, not an AST-dumping subprocess, textual scanner, compiler
fork or dynamically loaded compiler plugin. Clang parses and resolves C++;
the tool consumes the resulting semantic AST and validates that Clang can
construct each checked function's CFG. The initial transfer engine follows
the **structured AST corresponding to a bounded subset of that CFG**. It does
not run a general CFG-element solver or use Clang's flow-sensitive framework.
Keeping this distinction explicit avoids claiming support for unmodelled CFG
edges such as exceptions handled by catches or gotos.

The executable can use `compile_commands.json` through LibTooling's `-p` option
or explicit compilation flags after `--`. It runs semantic analysis and emits
source-located errors and optional JSON. Native compilation remains a separate
step; the supplied CMake integration makes the check a prerequisite.

## Syntax and vocabulary

An application owns `namespace=example::tags` and `labels=Persisted, ...` in a
small properties file. Generation emits a header of empty record declarations,
each marked with `clang::annotate("clangsolv.label")`. Namespace-qualified record
names provide label identity. Adding a label needs no checker/library change.

Generic macros attach AST annotations to function or parameter declarations:

| Syntax | Meaning |
| --- | --- |
| `returns(L...)` | Trusted successful return establishes L |
| `needs(L...)` | Parameter needs every listed label |
| `preserves(k)` | Successful result retains evidence of argument k, one-based; `preserves()` defaults to 1 |
| `readonly` | Trusted promise that the call does not invalidate caller evidence |

Names in macros are stringified and resolved against registered records.
Short names are allowed when unambiguous; fully qualified names are accepted.
Unknown/ambiguous names, misplaced annotations, invalid indices and unrelated
preservation domain types reject the check. Contracts are combined across
function redeclarations, so declarations in public headers govern their
definitions and calls in other translation units. Returns and effects are
trusted summaries, not inferred implementation proofs or cross-TU body analysis.

The generated header includes the reusable annotations/marker library. Non-Clang
native compiler builds erase macros. Analysis defines `CLANGSOLV_CHECKING` and
requires Clang. No promise is made that standard attributes alone enforce the
custom semantics: `clang::annotate` is a Clang extension using attribute syntax.

## Evidence model and transfer rules

An environment maps named automatic values and reference parameters to finite
sets of fully qualified label names. An absent entry means no evidence.
Parameters begin with their declared requirements. Joins intersect evidence
for every variable on reachable normal paths. Early returns and throws end the
current normal path. The engine also propagates separate break/continue exits.

1. A contracted call checks parameter requirements against argument evidence,
   then computes return labels union selected preservation-source evidence.
2. A call without readonly erases the current environment **after** capturing
   its argument evidence and checking its requirements. Its declared result
   can introduce a fresh state. Preservation concerns the result; it does not
   itself promise input immutability.
3. Initialization binds expression evidence. A direct variable assignment
   first erases all existing evidence, then binds the new destination evidence.
   This deliberately coarse policy covers unknown aliases. Member writes,
   compound assignments, increments and taking addresses erase the environment.
4. Member/element reads do not inherit labels from their containing object.
   Type-changing casts do not transfer evidence. Dangerous casts reject checks.
5. Defaulted copy and move operations transfer value evidence to the destination
   under the documented semantic-copy assumption. Moves erase source evidence;
   nontrivial defaulted copies also conservatively erase other evidence before
   binding their result. Custom constructors do not implicitly transfer labels.
6. Both alternatives of an `if` or conditional expression are considered; only
   common evidence survives. Short-circuit operators join the executed and
   skipped right-hand paths. There is no predicate solving or literal-condition
   pruning, so constant conditions still receive conservative treatment.

Reference declarations directly naming a variable share its evidence region.
Unknown aliases are handled by coarse invalidation, not a pointer-analysis
claim. References initialized through more complex expressions can have their
own conservative evidence binding; subsequent writes/calls still erase all
evidence. Global and static-local storage is not tracked. Pointer evidence and
proof markers on unnamed storage are rejected.

## Loops and reachability

`while` and `for` include the zero-iteration path. Starting at entry E, the loop
head iterates `H_next = E intersection Back(H)` until stable. `Back` includes
normal body and continue paths, with increment/condition effects where
appropriate. Break states contribute directly to exits. A `do` loop includes
at least one body execution. Requirements are diagnosed only at the converged
head, which catches facts invalidated before a later iteration.

The finite evidence domain and conservative transfer rules make descending
iteration terminate for supported code; a 1024-iteration safety limit rejects
any unexpected nonconvergence. Nested loop convergence is implemented with
diagnostics suppressed during exploratory passes and enabled for the stable
pass. A condition declaration in a loop is explicitly unsupported.

## Proof markers without wrappers

The small header library supplies no-op templates:

```cpp
clangsolv::establish<Persisted, Validated>(article); // trusted union
clangsolv::require<Persisted>(article);             // static requirement
clangsolv::forget<Validated>(article);              // selective revocation
clangsolv::forget(article);                        // discard all evidence
```

Only generated/registered label types are accepted. These functions do not
perform validation, allocate runtime label storage or modify the domain value.
Their calls must name a local value or reference parameter. Explicit establishment
is intentionally an escape hatch for audited application proof points.

## Supported scope and trust boundaries

Supported control flow is blocks, declarations, expressions, if/else, conditional
expressions, while/for/do, break/continue, return and standalone throw. Direct
function calls, ordinary records, supported copy/move operations and reference
parameters are checked. Available nondependent function bodies in user code
and headers are analysed; instantiated templates are visited. Uninstantiated
dependent bodies are deferred, like ordinary C++ instantiation-dependent checks.
System-header bodies are not analysed. Local variable contracts are unsupported;
requirements belong on parameters or explicit proof markers.

Calls with multiple side-effecting arguments and unsequenced side-effecting
binary operands are rejected to avoid assuming an evaluation order for nested
requirements. Member-operator contract indices count explicit parameters;
Clang's implicit receiver argument is accounted for separately.

Switch, goto, range-for, try/catch, coroutines, lambdas, indirect calls, virtual
dispatch, custom-destructor value construction, const_cast/reinterpret_cast and
unmodelled expressions reject the check. This list is not a complete C++ feature
matrix: the executable reports unsupported AST node kinds at their locations.
Custom destructor rejection covers direct checked constructions; this is not
a complete analysis of nested library-managed cleanup or hidden callbacks.

Returns, preservation, readonly and semantic fidelity of defaulted copies/moves
are trusted. In particular, a defaulted operation can call custom member
operations whose semantics the checker does not prove. readonly means preservation
of the caller's tracked facts, not absence of I/O: an output function can print
without invalidating a snapshot. Mutable members, pointers inside domain types,
aliases outside the analysed function, library callbacks, concurrent changes,
external persistence and credential expiry remain application responsibilities.
No general memory, effect, lifetime or whole-program soundness theorem is claimed.

Conservative erasure can reject valid programs. For example, incrementing an
unrelated loop counter may drop an article's evidence. Use small workflow
functions and truthful contracted boundaries. Do not annotate an arbitrary
mutation readonly merely to suppress errors. Future region/effect analysis
can improve precision without weakening current diagnostics.

## Build integration and report

The helper generates the vocabulary before checking and invokes the tool on
listed sources before native compilation. It always rechecks when the target
builds, so header changes cannot slip through a stale success stamp. Pass the
same preprocessor/include/target settings in OPTIONS; the helper does not
automatically infer arbitrary target properties. Standalone `-p` analysis uses
the actual compilation database and is preferred for complex builds. LLVM's
builtin resource-header path is captured at tool build time and can be overridden
using LibTooling `--extra-arg=-resource-dir=...` (last flag wins).

JSON records overall acceptance, compiler exit, checked functions/CFG blocks,
source files, requirement events (required, known, missing labels) and checker
diagnostics. Native parser diagnostics are printed by Clang, and parser failure
sets overall rejection; they are not duplicated into the checker diagnostic
array. The report is evidence about this check, not a domain proof certificate.

## Plan and completion evidence

1. Inspect existing CSOLV/JSOLV and find a real LLVM development installation.
2. Define a vocabulary-neutral annotation protocol and trusted effect boundary.
3. Implement LibTooling parsing, contract validation and structured flow analysis.
4. Add loop fixed points, aliases, proof markers and source-located JSON reporting.
5. Make checking mandatory in an application CMake build and demonstrate native
   workflow values, branches, extension and deliberate failures.
6. Exercise real LibTooling acceptance/rejection, compilation databases,
   generation and runtime application behaviour.
7. Write usage notes and a self-contained TeX sequel; compile and visually
   inspect the paper and stage authored files with Git.

The tool is built against LLVM/Clang 19.1.0 using MSVC 19.44 on Windows. The
application is checked by that frontend and compiled/run with native MSVC;
the accepted application also compiles/runs with GCC 13.3.0. The 81-test
regression suite uses actual LibTooling invocations. The environment's LLVM
package exported an obsolete DIA SDK path, handled by local imported-target
rebinding in CMake. LLVM/Clang 18+ is the intended API range; only 19.1.0 is
validated here. Future work: a general CFG solver, region-sensitive kills,
verified effect summaries, override/indirect-call contracts, expanded exception
and template coverage, compilation-database build gating and cross-platform CI.

The default preservation source is argument one. The macro stringifies its
argument unchanged; the checker interprets an empty payload as index 1. This
keeps the C++17 interface simple without preprocessor argument-count tricks.
Explicit indices retain their existing bounds and domain-type checks.

## Pointee labels: intent, plan and implemented policy

Keep application types ordinary, and interpret a smart-pointer contract as a
contract on its pointee. Avoid a heap graph in this version: copying a handle
copies the currently known evidence snapshot, while global invalidation
protects against stale aliases. This trades precision for a small specification
and leaves future identity-based analysis as an optional refinement.

Implementation steps: recognise standard ownership templates semantically;
normalise preservation domain types to their pointees; summarise dereference,
copy, move and const observers; invalidate on escape/replacement/mutation;
make revocation alias-safe; add accepted/rejected ownership tests and a runnable
showcase; document both trust boundaries and deliberate false negatives.
These steps are implemented. USAGE.md gives the exact supported operations.

No alias identity is inferred from a preservation summary: it promises labels
on the result, not that the result points to the input object. Smart-pointer
copies snapshot facts, so later establishment on one handle never silently
labels another. Forgetting kills other variables' facts regardless of type,
which also covers aliases borrowed as ordinary references. Moves and writes
kill the environment before the destination receives any fresh evidence.
Destruction effects, custom ownership machinery and concurrent mutation are
outside the trusted summaries; this is not general C++ memory-safety analysis.
