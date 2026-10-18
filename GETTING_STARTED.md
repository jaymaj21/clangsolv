# Getting started with ClangSOLV

This guide takes you from installing the tools to checking your own C++ project.
No experience with Clang's libraries or compiler plugins is assumed.

The Windows instructions follow the environment validated for this repository:
Windows x64, Visual Studio 2022/MSVC and LLVM/Clang 19.1.0. A Linux route is also
provided, but has not been validated in this workspace. The examples use C++17.
LLVM 18+ is the intended API range; only 19.1.0 has been validated here.

## 1. What you need to install

There are two separate compilations:

1. Build **ClangSOLV itself**, linking against LLVM/Clang development libraries.
   Normally you do this once when setting up your workstation.
2. Your application's build generates its label header, runs ClangSOLV over its
   source, and then compiles the application with your normal C++ compiler.

| Prerequisite | Purpose |
| --- | --- |
| Git | Download this repository |
| C++17 compiler and platform SDK | Build the checker and application |
| LLVM **and Clang development packages** | Headers, link libraries and CMake package files |
| CMake 3.20+ | Configure builds; CTest comes with CMake |
| Python 3.10+ | Generate label headers and run tests |
| Build backend | Visual Studio/MSBuild on Windows; Ninja or Make on Linux |

There are no Python packages to install with pip. LaTeX is needed only if you
want to rebuild the research papers.

**A working `clang++ --version` alone is insufficient.** You need
`LLVMConfig.cmake`, `ClangConfig.cmake`, Clang AST/LibTooling headers and their
link libraries. Python libclang bindings are not a replacement. Use matching
LLVM and Clang versions and a compatible architecture, compiler ABI and runtime.

## 2. Install prerequisites on Windows x64

### Install the native compiler and SDK

Install Visual Studio 2022 or its Build Tools, selecting **Desktop development
with C++**, the MSVC x64/x86 toolset and a Windows SDK. The IDE is optional if
you use Build Tools. Microsoft's [C++ installation guide](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170)
explains the workload selection.

Use the **Developer PowerShell for VS 2022** shortcut for these commands. It
sets up compiler and SDK paths. CMake's Visual Studio generator can also detect
an installation from ordinary PowerShell, but the developer shell makes compiler
problems easier to troubleshoot.

### Install Git, CMake and Python

Use the official installers:

- [Git for Windows](https://git-scm.com/install/windows).
- [CMake](https://cmake.org/download/): select the option to add CMake to PATH.
  The validated workspace uses CMake 3.30.4. A compatible CMake 3.x release is
  a useful fallback if newer CMake reports policy errors in LLVM package files.
- [Python](https://www.python.org/downloads/): install Python 3.10 or newer and
  make `python` available on PATH.

Open a new developer shell after installing them:

```powershell
git --version
cmake --version
ctest --version
python --version
```

If `python` opens the Microsoft Store, correct its Windows application execution
alias/PATH or use the installed interpreter's full path. CMake can select it
explicitly with `-DPython3_EXECUTABLE=C:/path/to/python.exe`.

### Obtain the full LLVM/Clang development distribution

Open the [LLVM 19.1.0 release](https://github.com/llvm/llvm-project/releases/tag/llvmorg-19.1.0)
and expand its assets. The x64 MSVC archive
`clang+llvm-19.1.0-x86_64-pc-windows-msvc.tar.xz` is one available distribution.
Use the full development distribution rather than assuming a compiler-only
installer has everything. See the [release asset list](https://github.com/llvm/llvm-project/releases/expanded_assets/llvmorg-19.1.0).

Extract it using an archive manager supporting `.tar.xz`. Some managers require
extracting the `.xz` layer and then the `.tar` layer. For this guide, place the
contents under `C:/Tools/llvm-19.1.0`. The installation root is the directory
containing `bin`, `include` and `lib`, not an outer extraction directory.

Check the installation:

```powershell
$llvmRoot = "C:/Tools/llvm-19.1.0"
Test-Path "$llvmRoot/lib/cmake/llvm/LLVMConfig.cmake"
Test-Path "$llvmRoot/lib/cmake/clang/ClangConfig.cmake"
Test-Path "$llvmRoot/include/clang/Tooling/Tooling.h"
& "$llvmRoot/bin/clang++.exe" --version
```

All three `Test-Path` commands should return `True`. Otherwise locate the actual
root or obtain a development distribution. CMake will also check whether the
link libraries are available. Installing Clang through Visual Studio does not
by itself establish that these development files exist.

You need not add LLVM to PATH: the build below specifies its package locations.

## 3. Build ClangSOLV on Windows

Choose a working directory. If you already cloned the repository, skip cloning
and change to that checkout instead.

```powershell
cd C:/Git
git clone https://github.com/jaymaj21/clangsolv.git
cd clangsolv
```

From the repository root:

```powershell
$llvmRoot = "C:/Tools/llvm-19.1.0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DLLVM_DIR=$llvmRoot/lib/cmake/llvm" "-DClang_DIR=$llvmRoot/lib/cmake/clang"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
& ./build/Release/clangsolv.exe --help
```

- **Configure** generates build files and finds dependencies.
- **Build** compiles and links the checker. Reduce parallelism if memory is tight.
- **Test** runs the checker regression suite. CTest lists one suite entry that
  currently invokes 81 Python tests internally.

The executable is `build/Release/clangsolv.exe`. Keep LLVM's installation in
place: the checker uses builtin headers from a resource-directory path captured
at configuration time. If you move LLVM, reconfigure or use the override below.

## 4. Linux installation and build alternative

On Ubuntu/Debian, install ordinary build tools:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build python3 git
```

You also need a matching LLVM/Clang 19 development installation. Where your
distribution offers version 19, relevant packages commonly include:

```sh
sudo apt install clang-19 llvm-19-dev libclang-19-dev libclang-cpp19-dev
```

Availability depends on your distribution and repositories. If these packages
are unavailable, consult the official [LLVM Debian/Ubuntu repository instructions](https://apt.llvm.org/)
for your distribution and an available version. Do not silently substitute the
newest LLVM and assume it was tested. Building LLVM 19.1.0 from source is another
option; see section 10.

Find the actual CMake package paths:

```sh
llvm-config-19 --version
llvm-config-19 --cmakedir
find /usr/lib/llvm-19 -name LLVMConfig.cmake -o -name ClangConfig.cmake
```

With the common `/usr/lib/llvm-19` layout:

```sh
git clone https://github.com/jaymaj21/clangsolv.git
cd clangsolv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm -DClang_DIR=/usr/lib/llvm-19/lib/cmake/clang
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/clangsolv --help
```

Replace those paths if your packages use a different layout. Ninja normally puts
the executable at `build/clangsolv`, without Visual Studio's `Release/` directory.
Install any additional development dependencies requested by your LLVM package
configuration. A WSL setup needs Linux dependencies throughout; Windows LLVM
libraries cannot be linked into a Linux checker.

## 5. Run the supplied application first

From the repository root, on Windows:

```powershell
$checker = (Resolve-Path ./build/Release/clangsolv.exe).Path
cmake -S example-app -B example-app/build -G "Visual Studio 17 2022" -A x64 "-DCLANGSOLV_CHECKER=$checker"
cmake --build example-app/build --config Release
ctest --test-dir example-app/build -C Release --output-on-failure
& ./example-app/build/Release/clangsolv_app.exe
```

On Linux:

```sh
cmake -S example-app -B example-app/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCLANGSOLV_CHECKER="$PWD/build/clangsolv"
cmake --build example-app/build
ctest --test-dir example-app/build --output-on-failure
./example-app/build/clangsolv_app
```

Expect `ClangSOLV: accepted` **before** native compilation, then published article
titles when running the app. The rejection test passes when four deliberately
invalid sources fail with missing-label diagnostics.

Files worth inspecting:

- [Label configuration](example-app/clangsolv.properties).
- [Contract-bearing header](example-app/include/workflow.hpp).
- [Ordinary workflow](example-app/src/main.cpp).
- [Smart-pointer example](example-app/src/smart_pointers.cpp).
- Generated header: `example-app/build/clangsolv-generated/clangsolv_app/clangsolv_labels.hpp`.
- Report: `example-app/build/clangsolv_app-clangsolv-report.json`.

## 6. Create your first checked project

Create a separate directory, such as `C:/Git/my-labelled-app`, with these three
files. Keep the ClangSOLV checkout available: its generator and library headers
are needed as well as its executable.

### File: `clangsolv.properties`

```properties
namespace=myapp::tags
labels=Persisted, Validated
```

This declares names, not validation logic. Add a name and rebuild to generate
another label; never edit the generated header. Use full-line comments. This
format does not implement Java-properties escapes or continuations.

### File: `main.cpp`

```cpp
#include <clangsolv_labels.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Document { int id; };
std::vector<Document> savedDocuments;

// Persisted means stored in this in-memory collection, not durable storage.
returns(Persisted) Document persist(Document document) {
    savedDocuments.push_back(document);
    return document;
}

readonly returns(Validated) preserves()
Document validate(const Document& document) {
    if (document.id <= 0)
        throw std::invalid_argument("id must be positive");
    return document;
}

readonly void publish(needs(Persisted, Validated) const Document& document) {
    std::cout << "Published document " << document.id << '\n';
}

int main() {
    const auto saved = persist(Document{42});
    const auto ready = validate(saved);
    publish(ready);
    // publish(saved); // Uncomment to see a missing-Validated error.
}
```

`returns` establishes result evidence, `needs` demands it, and `preserves()`
retains argument one's known labels while adding the result labels.
`preserves(2)` selects the second explicit argument.

`readonly` promises that a call preserves the caller's tracked facts; it does not
forbid printing. Returns, preservation and effects are trusted promises. The
checker verifies their use, not whether your domain validator is correct.

### File: `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_labelled_app LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLANGSOLV_ROOT "" CACHE PATH "Path to the ClangSOLV checkout")
set(CLANGSOLV_CHECKER "" CACHE FILEPATH "Path to the built checker executable")
if(NOT EXISTS "${CLANGSOLV_ROOT}/cmake/ClangSOLV.cmake")
  message(FATAL_ERROR "Set CLANGSOLV_ROOT to your ClangSOLV checkout")
endif()
if(NOT EXISTS "${CLANGSOLV_CHECKER}")
  message(FATAL_ERROR "Build ClangSOLV and set CLANGSOLV_CHECKER")
endif()

include("${CLANGSOLV_ROOT}/cmake/ClangSOLV.cmake")
add_executable(my_labelled_app main.cpp)
clangsolv_enable(my_labelled_app clangsolv.properties
  CHECKER "${CLANGSOLV_CHECKER}"
  SOURCES main.cpp)
```

From **your new application's directory**, on Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCLANGSOLV_ROOT=C:/Git/clangsolv -DCLANGSOLV_CHECKER=C:/Git/clangsolv/build/Release/clangsolv.exe
cmake --build build --config Release
& ./build/Release/my_labelled_app.exe
```

For Linux, substitute your actual paths:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCLANGSOLV_ROOT=/home/you/clangsolv -DCLANGSOLV_CHECKER=/home/you/clangsolv/build/clangsolv
cmake --build build
./build/my_labelled_app
```

Expected output: `Published document 42`.

Now uncomment `publish(saved)` and rebuild. Checking should fail because
`myapp::tags::Validated` is missing, preventing native compilation. Restore the
comment to make the build pass again. A failed build may leave an older executable
on disk; running it does not mean the changed source passed checking.

## 7. Add checking to an existing CMake project

Keep your normal executable/library target. Include the helper and call
`clangsolv_enable` for each target you want checked. For example:

```cmake
target_include_directories(my_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(my_app PRIVATE MY_FEATURE=1)

clangsolv_enable(my_app clangsolv.properties
  CHECKER "${CLANGSOLV_CHECKER}"
  SOURCES src/main.cpp src/workflow.cpp
  OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/include" "-DMY_FEATURE=1"
  DEPENDS include/workflow.hpp)
```

This excerpt assumes `my_app` and the checker path already exist and the helper
was included. List every relevant translation unit in `SOURCES`; headers are
parsed through includes. Put contracts in public headers so callers and definitions
see the same promises. Each target gets a separate generated-header directory.

The helper does **not** automatically infer arbitrary CMake target settings.
Supply matching include directories, definitions and target flags in `OPTIONS`.
Analysis starts in C++17 mode; explicitly align language options with native
compilation if you depart from these examples. Clang must be able to parse your
actual headers and platform configuration.

The helper generates labels, checks the listed sources, then permits native
compilation. It runs the check whenever the target builds, avoiding stale success
stamps after header changes. Keep this dependency enabled in CI. Commit handwritten
source and the configuration; ignore build output.

## 8. Understand the checking rules

Labels are independent positive facts. Missing evidence means unknown, not
proven false. Assignment and branch joins can lose facts. An annotation is not
a runtime validation check.

Unknown calls, writes and escapes conservatively erase evidence. Construct raw
objects and strings before gathering facts, and bind nested results to variables
if evaluation order triggers a diagnostic. Do not add `readonly` just to silence
an error: it must describe a truthful effect boundary.

References are supported. Labels on supported `std::shared_ptr<T>` and
`std::unique_ptr<T>` describe **the managed T object**, not the wrapper. Copies
retain evidence known at that moment; moves transfer it; mutation/reset/escape
invalidate evidence conservatively across aliases. Raw-pointer contracts, custom
ownership machinery and general lifetime checking remain unsupported.

The frontend also rejects syntax outside its supported fragment, including
try/catch, virtual dispatch, indirect calls, lambdas and range-for. It does not
prove producer postconditions, security policy, hidden mutation or concurrency.
Start with a small workflow around a meaningful consumer boundary. See
[USAGE.md](USAGE.md) for the API and [DESIGN.md](DESIGN.md) for exact limitations.

## 9. Standalone checks and reports

The helper is the easiest starting point. An existing build that exports
`compile_commands.json` can also be checked by replaying its compilation flags:

```sh
/path/to/clangsolv -p /path/to/project/build --report report.json /path/to/project/src/main.cpp
```

Sources and generated headers must already exist, and the database must include
the paths to the generated labels and ClangSOLV library headers. For supported
generators such as Ninja, CMake can export this file with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. A standalone invocation does not gate native
compilation by itself: wire failure into your build or CI dependency.

The helper writes `build/<target>-clangsolv-report.json` in the application's
build directory. It records requirements, available/missing labels, locations
and overall acceptance. Clang parsing errors appear on stderr; an empty custom
diagnostic array alone does not establish success.

## 10. Building LLVM from source as a fallback

Use this when no compatible development distribution is available. This is a
substantial compiler build requiring significant disk space, memory and time.
It is unnecessary when suitable development libraries are already installed.

From a tools workspace in a POSIX shell:

```sh
git clone --depth 1 --branch llvmorg-19.1.0 https://github.com/llvm/llvm-project.git
cmake -S llvm-project/llvm -B llvm-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=Native -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DLLVM_INSTALL_TOOLCHAIN_ONLY=OFF -DCMAKE_INSTALL_PREFIX="$HOME/llvm-19.1.0"
cmake --build llvm-build --parallel 2
cmake --install llvm-build
```

On Windows, use a developer shell, substitute
`-G "Visual Studio 17 2022" -A x64` and an absolute Windows installation prefix,
and add `--config Release` to build/install commands. Do not mix MSVC libraries
with a MinGW host compiler. Runtime settings must agree between LLVM and the
checker. Consult LLVM's [CMake build documentation](https://releases.llvm.org/19.1.0/docs/CMake.html).

Then point ClangSOLV's `LLVM_DIR` and `Clang_DIR` into that installation's
`lib/cmake/llvm` and `lib/cmake/clang` directories.

## 11. Troubleshooting

| Symptom | Check or remedy |
| --- | --- |
| Command not found | Install the tool, reopen the shell and check PATH |
| Missing LLVM/Clang CMake configuration | Obtain development packages; pass directories containing the config files, not filenames |
| Installation checks return `False` | Check the extraction root and package contents |
| MSVC or Windows headers missing | Install the C++ workload/SDK and use a developer shell |
| Architecture/runtime linker mismatch | Match LLVM's architecture, ABI and runtime; use a new build directory after switching toolchains |
| Missing `diaguids.lib` | The Windows workaround handles stale LLVM export paths; set `CLANGSOLV_DIAGUIDS_LIBRARY` to the installed DIA SDK's x64 library if discovery fails |
| Missing builtin headers such as `stddef.h` | Retain LLVM's resource directory; reconfigure with `-DCLANGSOLV_RESOURCE_DIR=/actual/lib/clang/19` or invoke the checker with `--extra-arg=-resource-dir=/actual/lib/clang/19` |
| Missing `clangsolv_labels.hpp` | Build through the helper; check generation and the generated include directory |
| New label unknown | Edit the correct config, rebuild and include the generated header; qualify ambiguous names |
| Evidence lost after an unrelated operation | Unknown effects are conservative; restructure or add a truthful contracted boundary |
| Valid C++ reported unsupported | Consult the supported subset; this is a bounded frontend |
| CTest lists only one checker test | That entry invokes the full Python suite |

Continue with [USAGE.md](USAGE.md), [DESIGN.md](DESIGN.md), or the
[paper collection](docs/papers/README.md) for more detail.
