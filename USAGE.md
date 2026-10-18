# Building and using ClangSOLV

New to the tool? Start with [the installation and first-project guide](GETTING_STARTED.md).

## Build the checker

Install matching LLVM and Clang **development** packages, including AST,
LibTooling headers, libraries and CMake package exports. A clang compiler-only
installation or Python libclang bindings are insufficient. Use a host compiler
and runtime compatible with those libraries. Tested: LLVM/Clang 19.1.0 and
MSVC 19.44 on Windows. CMake 3.20+ and Python 3.10+ are required.

Typical configure/build/test:

```sh
cmake -S . -B build -DLLVM_DIR=/path/to/lib/cmake/llvm -DClang_DIR=/path/to/lib/cmake/clang
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

In this workspace, the complete installation is outside PATH:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR=C:/clang_llvm_19.1.0/lib/cmake/llvm -DClang_DIR=C:/clang_llvm_19.1.0/lib/cmake/clang
cmake --build build --config Release
```

The executable is `build/Release/clangsolv.exe` for this generator. Single-configuration
generators generally place it at `build/clangsolv`. On Windows, a stale absolute
DIA SDK dependency in LLVM exports is rebound locally to the current Visual
Studio installation; set `CLANGSOLV_DIAGUIDS_LIBRARY` explicitly if needed.
The checker needs Clang builtin headers from the LLVM installation; CMake embeds
their location via `CLANGSOLV_RESOURCE_DIR`. Keep that installation available or
override the resource directory when checking.

## Build and run the showcase

```powershell
cmake -S example-app -B example-app/build -G "Visual Studio 17 2022" -A x64 -DCLANGSOLV_CHECKER=C:/Git/clangsolv/build/Release/clangsolv.exe
cmake --build example-app/build --config Release
ctest --test-dir example-app/build -C Release --output-on-failure
example-app/build/Release/clangsolv_app.exe
```

Use your actual absolute checker path. The build first generates labels, then
runs ClangSOLV on all three application translation units, then compiles with the
native compiler. Failure prevents application compilation. The report is
`example-app/build/clangsolv_app-clangsolv-report.json`.

## Configure application labels

```properties
namespace=example::tags
labels=Persisted, Validated, Authenticated, Approved
```

Include `<clangsolv_labels.hpp>`. Adding a name extends the vocabulary without
changing the tool or library. Generated label types are available for explicit
proof markers; contract macros may use unambiguous short names or qualified names.
Different namespaces mean different evidence identities. Unknown or ambiguous
names are errors. Use a shared vocabulary for related public APIs.

Blank lines and full-line `#`/`!` comments are accepted. The format does not
support Java-properties continuation/escape rules or inline comments. Invalid
identifiers, keywords, duplicates and unknown keys fail generation. Generated
headers live in the build directory and should not be hand-edited.

Manual generation:

```sh
python generator/generate_labels.py --config myapp/clangsolv.properties --output myapp/build/generated/clangsolv_labels.hpp
```

## Contracts on ordinary C++ values

```cpp
returns(Persisted)
Article persist(Article article);

readonly preserves() returns(Validated)
Article validate(const Article& article);

readonly
void publish(needs(Persisted, Validated) const Article& article);

Article chosen; // construct raw storage before gathering evidence
auto saved = persist(draft);
auto checked = validate(saved);
publish(checked); // accepted
// publish(saved); // rejected: missing Validated
```

`preserves()` defaults to `preserves(1)`. It transfers argument one's known evidence to the result, in addition
to its returns labels. Use `preserves(2)` to select the second argument.
It requires matching domain types, ignoring references
and cv qualification. It promises neither object identity nor preservation of
the input's own evidence. readonly separately promises that a call does not
invalidate the caller's tracked facts; without it the call erases those facts
after checking its arguments and capturing result evidence. An introducing
operation such as persist can therefore return fresh evidence while invalidating
old evidence elsewhere. These are trusted contracts, not body proofs.

Parameter requirements also seed evidence inside definitions, including when
their declaration is in another included header. Native C++ overload resolution
selects the actual function contract. Side-effecting multi-argument calls with
requirements/preservation are rejected to avoid guessing evaluation order;
bind those argument expressions first.

Multiple side-effecting arguments or unsequenced side-effecting binary operands
are also rejected, including nested calls. Bind their results before combining
them so the checker does not assume an evaluation order. Member-operator
preservation indices count explicit parameters, excluding the implicit receiver.

## Assignments, joins, loops and explicit proof points

```cpp
// chosen was declared before the evidence-bearing stages above.
if (condition) chosen = checked;
else chosen = saved;
clangsolv::require<example::tags::Persisted>(chosen); // common fact
// require<Validated>(chosen) would fail

chosen = validate(chosen); // fresh result replaces destination evidence
clangsolv::establish<example::tags::Approved>(chosen); // audited assertion
clangsolv::require<example::tags::Approved>(chosen);
clangsolv::forget<example::tags::Approved>(chosen);
clangsolv::forget(chosen); // forget all labels
```

Proof markers are runtime no-ops on existing values. Establishment unions
evidence; forgetting removes selected facts or all facts; requiring checks a
subset. Bind a named local value/reference or smart pointer first; unnamed expressions, raw pointers,
global and static-local storage are not supported proof-marker targets.

`while`/`for` joins include zero iterations and are checked at a loop fixed
point, so evidence lost during one iteration is unavailable in later iterations.
`do` executes at least once. Break, continue, early return, standalone throw
and short-circuit expressions have explicit flow rules. Branches are treated
conservatively even with literal conditions; no predicate solving is performed.

Writes and uncontracted calls erase **all** tracked evidence, a coarse alias-safe
approximation within the documented model. This can discard unrelated facts
and reject valid programs. Defaulted copy/move operations can transfer evidence
to destinations; moves erase source evidence. This assumes semantic fidelity of
domain copying, including member operations, and does not prove that assumption.
Keep workflow values as passive snapshots and trusted boundaries small.
Nontrivial constructors without a model can also erase existing evidence;
declare raw branch destinations before gathering workflow facts, as the
showcase does. A general region/effect model would improve this precision.

## Standalone checking and compilation databases

From the repository root, after building the application:

```powershell
build/Release/clangsolv.exe --report example-app/build/early-report.json example-app/examples/early.cpp -- -std=c++17 -Ilibrary/include -Iexample-app/include -Iexample-app/build/clangsolv-generated/clangsolv_app
```

This command intentionally fails with source-located missing-label diagnostics.
Replace the source with `src/main.cpp` to see acceptance. Report parents must
already exist. JSON requirement entries include consumer, location, required,
known and missing labels; overall rejection also covers Clang syntax errors.
Clang's native parser diagnostics go to stderr, not the checker diagnostic array.

For an existing build with `compile_commands.json`:

```sh
clangsolv -p myapp/build --report myapp/build/check.json myapp/main.cpp myapp/other.cpp
```

CMake Ninja/Makefile generators can emit the database with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`; Visual Studio generators do not emit it.
The driver accepts LibTooling options such as `--extra-arg=-DMY_FEATURE=1`.
Check every relevant translation unit with its real settings. Parsing through
Clang may require compatible flags/system headers even when the native backend
is GCC or MSVC.

## Mandatory integration in another CMake application

```cmake
include(path/to/clangsolv/cmake/ClangSOLV.cmake)
add_executable(my_app main.cpp workflow.cpp)
clangsolv_enable(my_app "${CMAKE_CURRENT_SOURCE_DIR}/clangsolv.properties"
  CHECKER "/absolute/path/to/clangsolv"
  SOURCES main.cpp workflow.cpp
  OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/include" "-DMY_FEATURE=1"
  DEPENDS include/workflow.hpp)
```

The helper always rechecks before building the target and generates its
vocabulary first. OPTIONS must reflect the target's real include/definition/
target settings; they are not inferred automatically from arbitrary CMake
properties. Configure the native target with matching settings as well. Use
standalone compilation-database checking for complex builds.

There are explicit scope restrictions: switch/goto/range-for/try-catch/coroutines,
lambdas, virtual and indirect calls, directly constructed custom-destructor
objects and unmodelled AST expressions are rejected. See DESIGN.md for details,
including templates, copying, shallow object state and external aliases.
The annotations are a Clang facility, not portable custom semantics defined by
the C++ standard. Non-Clang native compilation erases them; do not bypass the
mandatory check and assume your contracts remain enforced.

The public contract names are deliberately short, unprefixed macros: `returns`,
`needs`, `preserves` and `readonly`. Include the annotation header before
using them. The name `needs` leaves the C++20 `requires` keyword available
for ordinary language constraints. The examples use C++17.


Smart pointers carry labels on their **pointee**, never on their ownership wrapper:

```cpp
returns(Persisted) std::shared_ptr<Article> persistShared(Article draft);
readonly returns(Validated) preserves()
std::shared_ptr<Article> validateShared(const std::shared_ptr<Article>& article);
readonly void inspect(needs(Persisted, Validated) const Article& article);

void example() {
    auto saved = persistShared(Article{"Draft"});
    auto checked = validateShared(saved);
    auto alias = checked;
    inspect(*alias);
    clangsolv::require<example::tags::Validated>(alias);
}
```

`std::unique_ptr<T>` with its default deleter and `std::shared_ptr<T>` are
recognised by their semantic standard-library types, including type aliases.
`needs` on a smart-pointer parameter checks the pointee; `returns` describes
the returned pointee; preservation compares the underlying domain type T,
ignoring outer ownership wrappers and pointee const qualification.
Dereference and arrow access carry the same evidence. Proof markers applied
to a named smart pointer describe its managed object; establishment explicitly
asserts that a live pointee exists and satisfies the labels. An empty/default
pointer has no evidence. This is not an independent nullness or lifetime checker.

Shared-pointer copies inherit currently known evidence. Unique-pointer moves
transfer it to the destination; the source loses evidence. All moves currently
erase other tracked evidence as well. `reset`, `swap`, assignment, `get`,
`release`, mutation and unknown calls erase all tracked evidence, including
facts on other handles and borrowed references. Smart-pointer assignment does
not currently restore destination evidence: prefer initialisation from a
contracted factory. `forget` likewise erases other variables' evidence before
retaining unforgotten labels on its explicit target, to avoid stale aliases.
Const observers (`operator bool`, `use_count`, `unique`) preserve evidence.

This deliberately uses evidence snapshots, not a heap identity graph. Labels
established on one handle after an earlier copy do not automatically reach
that copy: use the labelled handle, or copy after establishing labels.
Branch and loop joins retain only common evidence. Copies cannot create
labels and revocation cannot leave usable stale evidence elsewhere.

Arrays, nested pointer pointees, custom unique-pointer deleters, adoption from
raw pointers and converting ownership constructors are rejected. Use
`make_shared`, `make_unique`, or trusted contracted factories. Factory summaries
must promise ordinary ownership and destruction without effects on other
tracked objects; hidden custom shared-pointer deleters are outside the model.
User-defined pointee destructors are rejected when visible. Concurrent mutation,
external aliases that change objects invisibly, and lifetime safety remain
outside the trusted sequential contract model. Raw-pointer contracts remain
unsupported. See `clangsolv-app/src/smart_pointers.cpp` for a runnable showcase.
