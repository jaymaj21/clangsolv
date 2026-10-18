# ClangSOLV application showcase

This application demonstrates workflow labels on ordinary `Article` objects:
demo persistence, title validation, demo author authentication and publication.
Its public [workflow header](include/workflow.hpp) carries the contracts, while
implementations are compiled separately. The programmer needs no wrapper types.

The application owns [clangsolv.properties](clangsolv.properties). The fourth
label, Approved, demonstrates extension without checker changes. Explicit
proof markers establish, require and revoke it. A native if/else demonstrates
intersection of evidence; subsequent workflow stages regain the missing facts.

Build the checker first, then from the repository root:

```powershell
cmake -S example-app -B example-app/build -G "Visual Studio 17 2022" -A x64 -DCLANGSOLV_CHECKER=C:/Git/clangsolv/build/Release/clangsolv.exe
cmake --build example-app/build --config Release
ctest --test-dir example-app/build -C Release --output-on-failure
example-app/build/Release/clangsolv_app.exe
```

Choose the actual checker path and generator for your platform. Checking is a
mandatory build dependency before native compilation. CTest runs the executable
and verifies the deliberately rejected examples: an early call, mutation after
checking, a branch with incomplete evidence and evidence lost in a later loop
iteration. These source files are excluded from the successful application build.

The JSON build report is `example-app/build/clangsolv_app-clangsolv-report.json`. Reports
include requirement locations and known/missing label sets. Full tool setup,
API details and standalone checking commands are in
[USAGE.md](../USAGE.md); supported semantics and trust boundaries are
in [DESIGN.md](../DESIGN.md).

The storage is an in-process demo vector and authentication uses a fixed demo
credential. They illustrate evidence introduction points, not production
persistence or authentication implementations. Their advertised contracts are
trusted; ClangSOLV checks how callers use those contracts.

Smart-pointer pointee labels are demonstrated in
`example-app/src/smart_pointers.cpp`: shared copies retain
known evidence, dereferencing transfers it, unique moves transfer ownership,
and mutation/reset/escape conservatively invalidate alias evidence. See the
checker usage guide for supported ownership operations and trust boundaries.
