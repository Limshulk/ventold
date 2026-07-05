# vent - game engine.
vent (lowercase!) is a high-performance, modular C++ game engine designed for flexibility and ease of use. It emphasizes asynchronous programming, abstraction, and module- & plugin-based architecture to allow developers to build complex graphical applications and games efficiently without dealing with low-level programming.
vent is a personal project with the goal of understanding engine programming and graphics programming.

## Core Principles ( not ordered by importance )

1. **async-only**: anything that can be async, must be async. always use multithreaded designs and utilize both GPU and CPU to its fullest.
2. **abstract-first**: keep client code simple, hide complexity. ease of use for game programmers is a priority.
3. **performance-aware**: low-level optimizations are important. measure, profile, and optimize critical paths.
4. **KISS**: Keep it simple, stupid.
5. **sdk-based**: The engine provides a SDK. any insights to engine-internals and engine-functionality via the sdk must be avoided.

# Coding Conventions

All agents and developers working in this workspace must adhere to the following coding conventions at all times. Failure to do so is unacceptable.
This file needs to be kept up-to-date if any changes occur that require addition or rewrites in here.

### General
- use modern C++23 features where possible. only use deprecated features if necessary for performance or compatibility.
- add comprehensive comments for any non-trivial code. code should always be understandable by others (non-engineer humans) and future you.
- prefer clarity over cleverness (KISS). write code that is easy to read and maintain, even if it is longer than the optimum. always note potential trade-offs in comments.
- do NEVER add workarounds, temporal implementations or hacks just to make something "work". always have the bigger architectural picture in mind and implement things properly.

### Modularity
- engine functionality should be dedicated to different interfaces & systems (always inside a module or plugin).
- implementations of specific features should be in different static libraries, called modules (f.e. input, render frontend, networking, audio ...).
- at compile time, the client should be able to select which static libraries to link against.
- a basic client application should therefore consist of:
    - `<application>`        : client executable or editor executable. a launcher. just loads modules and calls the main loop.
    - `libvent_core.so`      : core engine library with core functionality and the main loop. all static modules are linked into this dll.
    - `libvent_<plugin>.so`  : plugin: dynamic libraries for hot-reloadable plugins (rendering backends, platform backends, etc.).
    - `libvent_<project>.so` : plugin: dynamic library for game code, hot-reloadable at runtime.

### SDK
- the engine is sdk-based. it builds to a sdk (in `build/<platform>/<compiler>/<mode>/sdk/`). this sdk may be spread as it is and has to be able to provide full and complete engine functionality.
- additionally, clients are built to `build/<platform>/<compiler>/<mode>/apps/<client>`.
- the public footprint / sdk-accessable footprint MUST be AS SMALL AS POSSIBLE. everything that is not ACTIVELY REQUIRED by clients, MUST NOT BE EXPOSED to the sdk.
- any insights to engine-internals and engine-functionality via the sdk must be avoided.
- the sdk should be as easy-to-use as possible (see core principles) and as slim as possible.

### Build
- vent is build with gcc.
- use `windows-debug` as a cmake build preset when on windows.
- use `linux-debug` as a cmake build preset when on linux.

### Performance
- always consider performance implications of your code. prefer algorithms and data structures that are efficient for the task.
- avoid unnecessary allocations, copies, and expensive operations in performance-critical paths.
- if the code is kept simpler for readability, but has a measurable performance impact, always note this in comments.
- heavily comment any non-obvious optimizations or trade-offs made for performance reasons.
- always use of `constexpr` or `consteval` and compile-time computations where possible to reduce runtime overhead. 

### Renderer
- the renderer abstracts ALL api calls. 
- NEVER is ANY api-specific code allowed in client / other modules. only allowed in modules with api-specific names (e.g. `vulkan_backend_system`).
- ideally, the client developer NEVER has to even look into renderer modules and NEVER has to use any renderer calls.
- the frontend (module: renderer) has the central authority. backends just implement the frontend's api. no logic must be implemented by the backend except what is required to implement the frontend's api.

### Comments
- all lowercase, ending with period.
- every file should have a file header specified in `/.vscode/vent.code-snippets`.
- `/.vscode/vent.code-snippets` also contains seperator comments to keep distance between not directly related code sections inside of the same file.
- always use doxygen for any function declaration (lowercase, with period).
- comprehensively use inline notes: `// <explanation>.` or `// todo: <task>.` or others. always lowercase, with period.
- use `///<` for documenting struct members (lowercase, period).
- always explain any struct usage & layout in struct doc comment if not obvious. use `///<` comments to describe the memory layout of structs.
- comment liberally. err on the side of too many comments rather than too few, as this is a learning project.
- write comments as if the developer wrote them. they should serve as personal notes, reminders of past work, and api explanations.

### Naming
- `snake_case` (e.g., `vent`, `_initialized`, `window_handle`) for EVERYTHING. the only exception: template typenames (`typename NAME`) are ALWAYS FULLY UPPERCASE.
- member variables: `_` prefix (e.g., `_initialized`, `_stats`).
- interfaces (client-faced): `ic_` prefix (e.g., `ic_memory`, `ic_log`).
- interfaces (engine-faced): `i_` prefix (e.g., `i_memory`, `i_log`).
- roles: `ir_` prefix (e.g., `ir_dependencies`, `ir_client`).
- interface implementations: no prefix, `_system` suffix (e.g., `memory_system`, `log_system`).

### Declarations & Definitions
- prefer `auto` for variable declarations when the (verbose) type is obvious from the initializer.
- use trailing return types for all functions (even simple ones) for consistency.

### File Organization
- headers: `#pragma once` (no ifdef guards).
- namespace: `namespace vent {` with `}  // namespace vent` closing.
- no indentation inside namespace.
- no `using namespace` in headers.
- includes MUST be sorted by accessibility in the following order and be ordered alphabetically in each group, seperated with a blank line:
1. local (module-private).
2. semi-global (other modules).
3. global (e.g., \_vent\ folders).
4. externals (e.g., vulkan, glfw, etc.).
5. C/C++ standard library.
- exceptions of that are _vent/_vent.hpp as a general single-include for all client usage of the engine and _vent/vent_sdk.hpp for general sdk usage. they must be ordered as the very first includes.

### Error Handling
- log context via `ic_log` before returning error. use full (`_f`) if more information may be required.
- return `bool` (false = failure).

### Struct & Class Layout Documentation
- for aligned structs (`alignas(N)`), document byte offsets and sizes.
- format: `type name;  ///< 0xNN-0xNN (Mb): description.`
- note auto-padding with `///< 0xNN-0xNN (Nb): padding.`
- add `static_assert(sizeof(T) == N)` to verify struct size.
- explain why alignment matters in struct doc comment.
- classes: public section with constructor, destructor and, if required, `VENT_NO_MOVE` or similar macros first. then: private section, protected, then public. always order members & methods by relevance. always put members before methods.

### NO Legacy Support
- the engine is in a very early stage and still in architectural development.
- do NOT add legacy support or backward compatibility code. there has never been a single project using vent.
- it is TOTALLY FINE if any new code breaks old code. that old code needs to be fixed then.
- do NOT add deprecated comments. always remove old code instead of deprecating it. keep a clean, minimal, definitive codebase.
- always remove any unused or deprecated code when encountered.

# Directory Structure
```
vent/                       # root folder
  .agents/                  # agent instructions
  .vscode/                  # vscode / antigravity-specific files and configurations
  artifacts/                # ai-generated artifactis that i decided to save manually. might contain interesting things for agents to study
  build/                    # build files - gitignored
  cmake/                    # cmake functions required for building
  logs/                     # log outputs when run with a debugger
  source/                   # all engine source files
    vent_apps/                # client programs that use the vent engine
      minimal/                  # client application example and developing testbed
        assets/                   # application-specific assets like shaders, models, ...
          ..
        src/                      # application source files
          ..
    vent_engine/              # core engine source code
      _vent/                    # PUBLIC VENT SDK. accessable by EVERYONE
        ..                        # folders to structure the sdk
        _vent.hpp                 # the one and only global include used by clients, NOT used by engine source files
        vent_sdk.hpp              # basic sdk definitions used all throughout vent
      assets/                   # engine-specific assets like shaders, models, ...
      modules/                  # engine modules.
        core/                     # base-engine module containing core functionality
          private/                  # module-private headers, not accessible by other modules.
          public/core/              # engine-private headers, not accessible by the client but accessible by other modules
          src/                      # .cpp files of that module
        ..                        # more modules with the same folder structure
      plugins/                  # engine plugins.
      templates/                # source templates that are copied to the public sdk.
```