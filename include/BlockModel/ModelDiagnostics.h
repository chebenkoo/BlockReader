#pragma once

#include <mutex>
#include <cstddef>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX   // prevent windows.h from defining min/max macros
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace Mining::ModelDiagnostics {

// Plain non-recursive mutex. The lock scope in the main thread must cover
// only the data swap — never Qt signals or markDirty() calls. This keeps
// the mutex free by the time getInstanceBuffer() tries to acquire it,
// eliminating any possibility of re-entrancy. (C++ Concurrency in Action,
// ch.3: "lock the minimum data for the minimum time; never call user code
// while holding a lock".)
inline std::mutex& modelMutex()
{
    static std::mutex m;
    return m;
}

// Returns current process Working Set in MB (Windows).
// Returns 0 on non-Windows builds.
inline size_t processMemoryMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / (1024ULL * 1024ULL);
#endif
    return 0;
}

} // namespace Mining::ModelDiagnostics
