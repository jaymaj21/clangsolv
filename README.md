# ClangSOLV: Clang Static Object Labelling for Verification

ClangSOLV is a **standalone Clang LibTooling frontend** that checks application-owned
workflow facts on ordinary C++ values. No state wrapper or modified compiler is
needed. Generated declarations register the vocabulary; contract macros attach
Clang AST metadata. The checker tracks evidence through assignments, native
branches and structured loops and reports missing labels at consumers.

```properties
namespace=example::tags
labels=Persisted, Validated, Authenticated, Approved
```

```cpp
returns(Persisted) Article persist(Article article);
readonly returns(Validated) preserves()
Article validate(const Article& article);
readonly
void publish(needs(Persisted, Validated) const Article& article);

auto saved = persist(draft);
auto checked = validate(saved);
publish(checked); // accepted
publish(saved);   // error: missing Validated
```

Return labels, preservation and readonly are **trusted contracts**. The checker
verifies their use, not their truth. Unknown calls and writes conservatively
erase evidence; unsupported operations reject the check. The implementation is
a useful bounded frontend, not a soundness claim for arbitrary C++.

- [Start here: installation and your first checked project](GETTING_STARTED.md)
- [Design, philosophy, plan and semantic boundaries](DESIGN.md)
- [Build, API and integration instructions](USAGE.md)
- [Showcase application](example-app/README.md)
- [Self-contained TeX article](docs/papers/clangsolv.tex)

Build with matching LLVM/Clang development packages (tested on 19.1.0), CMake
3.20+, Python 3.10+ and a compatible host C++17 compiler:

```sh
cmake -S . -B build -DLLVM_DIR=/path/to/lib/cmake/llvm -DClang_DIR=/path/to/lib/cmake/clang
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The application CMake integration makes checking mandatory before compilation.
GCC and MSVC can compile the accepted application: on non-Clang backends macros
erase to ordinary C++ declarations. **That backend compilation alone does not
enforce labels.** Keep the checking step enabled. CSOLV remains the companion
portable template library for projects requiring native type-based enforcement.

Smart-pointer pointee labels are demonstrated in
[`example-app/src/smart_pointers.cpp`](example-app/src/smart_pointers.cpp): shared copies retain
known evidence, dereferencing transfers it, unique moves transfer ownership,
and mutation/reset/escape conservatively invalidate alias evidence. See the
checker usage guide for supported ownership operations and trust boundaries.

## Run the application

Build the checker first, then configure the independent example with its
absolute executable path. On Windows with Visual Studio:

```powershell
cmake -S example-app -B example-app/build -G "Visual Studio 17 2022" -A x64 -DCLANGSOLV_CHECKER=C:/Git/clangsolv/build/Release/clangsolv.exe
cmake --build example-app/build --config Release
ctest --test-dir example-app/build -C Release --output-on-failure
example-app/build/Release/clangsolv_app.exe
```

For another platform or generator, use the actual checker path (often
`build/clangsolv`) and executable location. See [USAGE.md](USAGE.md) for the
LLVM development-package setup, compiler compatibility and integration flags.

The application checks three translation units before native compilation. It
demonstrates persistence, validation, authentication, branch joins, explicit
proof markers, shared-pointer copies and unique-pointer moves. Four deliberately
invalid examples verify rejection of missing evidence. The demo store and
credential illustrate contracts rather than production persistence or security.

## The contract model

| Construct | Meaning |
| --- | --- |
| `returns(Label, ...)` | Successful result establishes those labels |
| `needs(Label, ...)` | Parameter requires every listed label |
| `preserves()` | Result retains argument one's known labels |
| `preserves(2)` | Result instead retains argument two's known labels |
| `readonly` | Trusted promise to preserve the caller's tracked evidence |
| `clangsolv::establish<Label>(value)` | Explicit trusted introduction |
| `clangsolv::require<Label>(value)` | Check a requirement at this program point |
| `clangsolv::forget<Label>(value)` | Revoke evidence conservatively |

Indices start at one and exclude an implicit member receiver. Labels are
independent positive facts; missing evidence is not proof that a property is
false. Their qualified namespaces determine identity. The generator owns names;
function declarations own behavior, so adding a label needs no checker edits.

For supported `std::shared_ptr<T>` and `std::unique_ptr<T>`, labels describe
**the pointee**. Shared copies inherit currently known evidence; unique moves
transfer it and invalidate the source. Mutation, reset and pointer escape
conservatively erase evidence across aliases. This uses evidence snapshots,
not a precise heap graph: labels established later on one handle do not
automatically propagate to an earlier copy. Raw-pointer contracts, custom
ownership machinery and general lifetime checking remain unsupported.

## Repository layout

| Path | Contents |
| --- | --- |
| `checker/` | Standalone LibTooling checker |
| `library/include/clangsolv/` | Contract macros and no-op proof markers |
| `generator/` | Configurable label-header generator |
| `cmake/` | Mandatory-check CMake helper |
| `tests/` | 81 real end-to-end regression tests |
| `example-app/` | Independent application and intentionally rejected examples |
| `docs/papers/` | Implementation, synthesis and background papers in TeX/PDF |

The application is a separate CMake project so it exercises normal consumer
integration with a built checker. Generated headers and reports live in build
directories. Build output, binaries, Python caches and LaTeX intermediates are
ignored; source, configuration, documentation and curated PDFs belong in Git.

## Papers and background

- [ClangSOLV paper](docs/papers/clangsolv.pdf)
  · [LaTeX](docs/papers/clangsolv.tex)
- [Comprehensive synthesis: ideas, JSOLV and ClangSOLV](docs/papers/static_object_labelling.pdf)
  · [LaTeX](docs/papers/static_object_labelling.tex)
- [Original static-property proposal](docs/papers/static_property_tags.pdf)
  · [LaTeX](docs/papers/static_property_tags.tex)
- [Original semantic-category proposal](docs/papers/semantic_roles_beyond_class.pdf)
  · [LaTeX](docs/papers/semantic_roles_beyond_class.tex)
- [Paper collection and companion implementations](docs/papers/README.md)

The papers also discuss JSOLV's Java annotations and CSOLV's portable template
library. Their papers are included for context; this repository contains the
ClangSOLV implementation. The research originated in
[jmtools](https://github.com/jaymaj21/jmtools); historical artifact maps in the
background papers refer to that workspace.
