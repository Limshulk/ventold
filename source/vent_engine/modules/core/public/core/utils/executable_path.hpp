#pragma once
//
// core module.
// executable path discovery.
// ——————————————————————
//
// answers "where is the running executable?" — the anchor for every
// deployment-relative path in the engine. asset mounts resolve against this
// instead of the current working directory, so an app behaves identically
// whether launched from a terminal, a debugger, or a double-click in a file
// browser. this is one of the two platform-specific dirt spots the core
// module exists to bury (the other being dynamic library loading).

#include <_vent/vent_sdk.hpp>

#include <filesystem>

namespace vent {

/// @brief get the directory containing the running executable.
/// @return absolute path to the executable's directory, or an empty path if
/// the os query failed (callers should treat that as a fatal setup error).
[[nodiscard]]
auto get_executable_directory() -> std::filesystem::path;

}  // namespace vent
