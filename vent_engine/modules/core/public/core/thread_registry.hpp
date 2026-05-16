#pragma once
//
// core module.
// thread registry.
// ——————————————————————
//
// header-only registry that maps kernel-hashed (random) thread id's to nice
// 4-char names.
// used for debugging purposes and by the log system to display the current
// thread in a readable manner.
//
// usage:
//   thread_registry::register_thread("<name>"); // call after thread creation.
//   thread_registry::unregister_thread();       // unregister current thread.
//   auto name = thread_registry::get_thread();  // returns current thread's
//   name.

#include <_vent/vent_sdk.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <format>
#include <thread>

#ifdef VENT_WINDOWS
    #include <windows.h>  // NOMINMAX & WIN32_LEAN_AND_MEAN are defined in vent_sdk.hpp
#elif defined(VENT_LINUX)
    #include <sys/syscall.h>
    #include <unistd.h>
#endif  // todo: macos is the same as linux?

namespace vent {

class thread_registry final {
private:
    thread_registry()  = default;
    ~thread_registry() = default;

    VENT_NO_COPY_MOVE(thread_registry);

private:
    // thread registry storage.
    // each thread is stored as a std::string.
    static inline std::unordered_map<u32, std::string> s_map;

    // we are not expecting many calls to this registry, so a blocking
    // thread-safety mechanism is fine.
    static inline std::shared_mutex s_mutex;

    /// @brief get thread id based on platform definition. caches it for fast
    /// access.
    /// @return cached thread id.
    static auto tid() -> u32 {
        static thread_local u32 id = get_current_thread_id();
        return id;
    }

    /// @brief get thread-local name (avoids locks when reading own name).
    /// @return reference to the thread-local name string.
    static auto local_name() -> std::string& {
        static thread_local std::string name;
        return name;
    }

public:
    /// @brief maximal name length. 4 characters + null terminator.
    static constexpr usize MAX_LEN = 4;

    /// @brief register the current thread with a name.
    /// @param name name for this thread (should be exactly MAX_LEN-1 chars).
    static void register_thread(std::string_view name) {
        if (name.empty())
            return;

        // save in thread_local static storage. cut to MAX_LEN characters.
        // local_name() gives a reference (&) to a thread-local static string,
        // that we can set.
        local_name() = name.substr(0, MAX_LEN);

        // save in global map.
        std::unique_lock lock(s_mutex);
        s_map[tid()] = local_name();
    }

    /// @brief unregister the current thread.
    static void unregister_thread() {
        local_name().clear();
        std::unique_lock lock(s_mutex);
        s_map.erase(tid());
    }

    /// @brief get the name for the current thread.
    /// @param id thread id to look up. defaults to current thread id.
    static std::string get_name(u32 id = tid()) {
        // use cache for own thread. this is fast.
        if (id == tid() && !local_name().empty())
            return local_name();

        // if it is another thread, check the map.
        {
            std::shared_lock lock(s_mutex);
            if (s_map.contains(id))
                return s_map.at(id);
        }

        // fallback: just use the thread id formatted as a 4-digit number.
        return std::format("{:04}", id % 10000);
    }

    /// @brief get the os-dependent thread id.
    /// @return thread id.
    static auto get_current_thread_id() -> u32 {
#if defined(_WIN32)
        return static_cast<u32>(::GetCurrentThreadId());
#elif defined(__linux__)
        return static_cast<u32>(syscall(SYS_gettid));
#else
        // fallback: hashed st::thread::id.
        auto id = std::this_thread::get_id();
        return static_cast<u32>(std::hash<std::thread::id> {}(id));
#endif
    }
};

}  // namespace vent