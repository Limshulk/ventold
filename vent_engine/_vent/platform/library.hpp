#pragma once
//
// vent public sdk.
// library utilities.
// ——————————————————————
//
// defines basic platform-independent library functions used within the vent
// engine. contains implementations as it is the only file that is used before
// the engine shared library is loaded. but don't worry, there ain't no secrets
// in this file :) .

#include <string>

#ifdef VENT_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif defined(VENT_LINUX) || defined(VENT_MACOS)
    #include <dlfcn.h>
#endif

namespace vent::lib {

// shared library prefixes and suffixes

#ifdef VENT_WINDOWS
constexpr const char* shared_lib_prefix = "lib";
constexpr const char* shared_lib_suffix = ".dll";
#elif defined(VENT_LINUX)
constexpr const char* shared_lib_prefix = "lib";
constexpr const char* shared_lib_suffix = ".so";
#elif defined(VENT_MACOS)
constexpr const char* shared_lib_prefix = "lib";
constexpr const char* shared_lib_suffix = ".dylib";
#else
    #error "Unsupported platform"
#endif

/// @brief create a platform-specific shared library name from a base name
/// without prefix or suffix.
/// @param base_name base name of the library without prefix or suffix.
/// @return platform-specific shared library name as std::string.
inline auto make_shared_library_name(const char* base_name) -> std::string {
    return std::string(vent::lib::shared_lib_prefix) + base_name +
           std::string(vent::lib::shared_lib_suffix);
}

#ifdef VENT_WINDOWS
using library_handle                            = HMODULE;
constexpr library_handle INVALID_LIBRARY_HANDLE = nullptr;
#else
using library_handle                            = void*;
constexpr library_handle INVALID_LIBRARY_HANDLE = nullptr;
#endif

/// @brief load a shared library from the given path.
/// @param path path to the shared library to load.
/// @return handle to the loaded shared library. INVALID_LIBRARY_HANDLE on
/// failure.
inline auto load_library(const char* path) -> library_handle {
#if defined(VENT_WINDOWS)
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
}

/// @brief unload a previously loaded shared library.
/// @param handle handle to the shared library to unload.
inline auto unload_library(library_handle handle) -> void {
    if (handle == INVALID_LIBRARY_HANDLE)
        return;

#if defined(VENT_WINDOWS)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

/// @brief get a symbol from a shared library.
/// @param handle handle to the shared library.
/// @param symbol_name name of the symbol to get.
/// @return pointer to the symbol. nullptr on failure.
inline auto get_library_symbol(library_handle handle, const char* symbol_name)
    -> void* {
    if (handle == INVALID_LIBRARY_HANDLE || symbol_name == nullptr)
        return nullptr;

#if defined(VENT_WINDOWS)
    return reinterpret_cast<void*>(GetProcAddress(handle, symbol_name));
#else
    return dlsym(handle, symbol_name);
#endif
}

/// @brief get a symbol from a shared library and cast it to the given type.
/// @tparam T type to cast the symbol to.
/// @param handle handle to the shared library.
/// @param symbol_name name of the symbol to get.
/// @return the symbol casted to the given type. nullptr on failure.
template <typename T>
auto get_library_symbol_as(library_handle handle, const char* symbol_name)
    -> T {
    return reinterpret_cast<T>(get_library_symbol(handle, symbol_name));
}

/// @brief get the last error message from the library loading system.
/// @return last error message as std::string.
inline auto get_last_error() -> std::string {
#if defined(VENT_WINDOWS)
    DWORD error = GetLastError();
    if (error == 0)
        return "";

    LPSTR buffer = nullptr;
    DWORD size   = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr,
                                error,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                reinterpret_cast<LPSTR>(&buffer),
                                0,
                                nullptr);

    std::string message(buffer, size);
    LocalFree(buffer);
    return message;
#else
    const char* error = dlerror();
    return error ? std::string(error) : "";
#endif
}

}  // namespace vent::lib