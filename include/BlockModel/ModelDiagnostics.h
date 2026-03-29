#pragma once

#include <mutex>

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

} // namespace Mining::ModelDiagnostics
