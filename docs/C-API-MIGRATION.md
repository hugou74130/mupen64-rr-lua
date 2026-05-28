# C API Surface Migration — Issue #616

> **Status:** 🚧 WORK IN PROGRESS — NOT YET TESTED
>
> This document tracks ongoing work to convert the C++ core API surface (`core_api.h`) into standard C, as requested in [issue #616](https://github.com/mupen64/mupen64-rr-lua/issues/616).

## Goals

1. **Remove C++ STL dependencies** from the public API headers.
2. **Expose a pure C interface** (`extern "C"`) for cross-language interop.
3. **Package the core as a dynamic library** (`.dll` / `.so` / `.dylib`).
4. **Enable frontend bindings** in safer languages (C#, Rust, etc.).

## Current State

| Task | Status |
|------|--------|
| Audit `core_api.h` for C++-only types | 🔲 Not started |
| Design C-compatible struct / handle layout | 🔲 Not started |
| Replace `std::string` with `const char*` | 🔲 Not started |
| Replace `std::vector` with size + pointer pairs | 🔲 Not started |
| Add `extern "C"` wrapper layer | 🔲 Not started |
| Update build system (CMake) for `SHARED` target | 🔲 Not started |
| Write C# / Rust binding PoC | 🔲 Not started |
| CI validation across platforms | 🔲 Not started |

## Known Blockers

- Heavy use of C++ templates and STL containers inside `core_api.h`.
- Callback signatures currently pass C++ objects by reference.
- Need to decide on opaque handle strategy (`typedef struct M64Core* M64CoreHandle;`).

## Notes

- This PR is opened as a **draft** to track progress publicly.
- **No code changes have been merged yet.** All implementation work happens on this branch.
- Early feedback on the design direction (especially from @Aurumaker72 and @abart27) is welcome.

---

*Last updated: 2026-05-28*
