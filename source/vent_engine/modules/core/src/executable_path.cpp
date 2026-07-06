// core module.
// executable path discovery implementation.
// ——————————————————————
//
// one #ifdef, one function: GetModuleFileNameW on windows, /proc/self/exe on
// linux. both return the launcher executable's path even though this code
// lives in the engine dll — GetModuleFileNameW(nullptr) explicitly queries
// the process executable, and /proc/self/exe is process-scoped by definition.

#include <core/utils/executable_path.hpp>

#ifdef VENT_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#include <array>

namespace vent {

auto get_executable_directory() -> std::filesystem::path {
#ifdef VENT_WINDOWS
    // wide api on purpose: ansi GetModuleFileNameA mangles non-ascii user
    // names / install paths. 32k is the extended windows path limit.
    std::array<wchar_t, 32768> buffer {};
    const DWORD                length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(buffer.data()).parent_path();
#else
    // /proc/self/exe is a symlink to the running executable. readlink does
    // not null-terminate, so pass the byte count explicitly.
    std::array<char, 4096> buffer {};
    const ssize_t          length =
        ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(buffer.data(),
                                             static_cast<size_t>(length)))
        .parent_path();
#endif
}

}  // namespace vent
