#pragma once
//
// core module.
// thread registry.
// ——————————————————————
//
// header-only registry that is responsible for easy thread creation. it cares
// about thread lifecycles and os-dependent safe-exits.
// also maps kernel-hashed (random) thread id's to friendly 4-char names. used
// for debugging purposes and by the log system to display the current thread in
// a readable manner.
//
// usage:
//   thread_registry::spawn_thread("<name>", [*](*) {<thread_function()>});
//      spawns a new thread executing thread_function(). handles creation and
//      cleanup, so nothing else is required to do.
//
//   thread_registry::register_thread("<name>"); // call after manual thread
//      creation to assign a name (generally not needed to call).
//
//   thread_registry::unregister_thread();       // unregister current thread
//      before it exits (generally not needed to call).
//
//   auto name = thread_registry::get_thread();  // returns current thread's
//      name.
//
//   auto name = thread_registry::get_thread(id);  // returns thread's
//      name from id.

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
    // prevent instantiation.
    thread_registry()  = default;
    ~thread_registry() = default;

    VENT_NO_COPY_MOVE(thread_registry);

private:
    // thread registry storage.
    // each thread is stored as a std::string.
    static inline std::unordered_map<u32, std::string> s_map;

    // we are not expecting many calls to this registry, so a blocking
    // thread-safety mechanism is fine
    static inline std::shared_mutex s_mutex;

    /// @brief get thread id based on platform definition. caches it for fast
    ///        access.
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
    /// @param name name for this thread (must be exactly MAX_LEN-1 chars).
    /// convention: 4 characters.
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

    /// @brief cleanly exit the current thread. called from inside the thread.
    /// @note this is intentional and load-bearing: on this toolchain, simply
    /// returning from the thread lambda was observed to hang/mis-behave on
    /// teardown, and an explicit os-level thread exit was the only reliable
    /// fix. keep it.
    // todo: revisit once the root cause (likely a static/tls destructor
    // ordering issue across the engine dll boundary) is understood -
    // exit_thread() skips c++ stack unwinding, so the thread lambda's captures
    // are not destroyed (an accepted trade-off for now).
    static void exit_thread() {
#ifdef VENT_WINDOWS
        ExitThread(0);
#elif defined(VENT_LINUX)
        pthread_exit(nullptr);
#else
        std::exit(0);
#endif
    }

    /// @brief spawn a new std::thread, automatically registering it and
    ///   handling cleanup.
    /// @tparam F the function type to execute.
    /// @param name the name of the thread, must be MAX_LEN characters.
    /// @param f the function to execute on the new thread.
    /// @return the spawned std::thread.
    template <typename F>
    static auto spawn_thread(std::string name, F&& f) -> std::thread {
        return std::thread(
            [name = std::move(name), func = std::forward<F>(f)]() mutable {
                register_thread(name);

#if defined(VENT_LINUX)
                std::string os_name = std::format("vent:{}", name);
                pthread_setname_np(pthread_self(), os_name.c_str());
#endif

                // run the user's thread function.
                func();

                // automatically unregister and cleanly terminate the thread.
                unregister_thread();

                exit_thread();
            });
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