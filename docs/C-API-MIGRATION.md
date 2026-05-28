# C API Surface Migration — Issue #616

> **Status:** 🚧 WORK IN PROGRESS — NOT YET TESTED
>
> This document tracks ongoing work to convert the C++ core API surface (`core_api.h`) into standard C, as requested in [issue #616](https://github.com/mupen64/mupen64-rr-lua/issues/616).

## Goals

1. **Remove C++ STL dependencies** from the public API headers.
2. **Expose a pure C interface** (`extern "C"`) for cross-language interop.
3. **Package the core as a dynamic library** (`.dll` / `.so` / `.dylib`).
4. **Enable frontend bindings** in safer languages (C#, Rust, etc.).

## Implementation Plan (3 Phases)

### Phase 1 — API Survey

Map `core_api.h` + all included headers, **and the config library**.

| Task | Approach | Status |
|------|----------|--------|
| Map `core_api.h` + included headers | Full dependency graph of C++-only types | 🔲 Not started |
| Map config library | Identify STL usage in config layer | 🔲 Not started |
| STL → C replacements | `std::string` / `std::vector` become `(pointer, size)` pairs in parameters | 🔲 Not started |
| Return value strategy | `malloc()`-backed struct with pointer+size, caller `free()`s | 🔲 Not started |
| Callback userdata analysis | Most core functions are raw functions or lambdas — per-callback `void* userdata` may be unnecessary | 🔲 Not started |
| Heap vs. in-out buffers | Catalog which return types need heap-allocated output vs. caller-provided in-out buffers | 🔲 Not started |

### Phase 2 — C Header + C++ Wrapper

| Task | Approach | Status |
|------|----------|--------|
| Draft `m64core.h` | Opaque handles (`typedef struct M64Core* M64CoreHandle;`) and flat C functions | 🔲 Not started |
| C++ convenience wrapper | Thin C++ layer over the C surface so the GUI side stays clean | 🔲 Not started |
| Build target | `m64core.dll` / `libm64core.so` via CMake `SHARED` | 🔲 Not started |

### Phase 3 — Incremental PRs

| Task | Approach | Status |
|------|----------|--------|
| POC subset | ROM loading + frame stepping as minimal viable subset | 🔲 Not started |
| Expand in chunks | Reviewable PRs per subsystem (video, audio, input, RSP, config) | 🔲 Not started |
| Config library | Separate follow-up PR for config layer migration | 🔲 Not started |
| Cross-platform CI | Validate on Windows (MSVC), Linux (GCC/Clang), macOS | 🔲 Not started |
| Binding PoC | C# / Rust / Python proof-of-concept frontend | 🔲 Not started |

## Known Blockers

- Heavy use of C++ templates and STL containers inside `core_api.h`.
- Callback signatures currently pass C++ objects by reference.
- Config library is deeply intertwined with the C++ STL.

## Notes

- This PR is opened as a **draft** to track progress publicly.
- **No code changes have been merged yet.** All implementation work happens on this branch.
- Early feedback on the design direction (especially from @Aurumaker72 and @abart27) is welcome.

---

*Last updated: 2026-05-28*
