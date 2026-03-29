# Crash Report: Threading Mutex Re-Entrancy Crash

**Project:** BlockReader / MiningSchedule
**Date:** 2026-03-29
**Symptom:** App terminates abnormally immediately after `[MAIN] setModel: 0 ms` is logged.
**Model loaded:** 2,728,709 blocks from CSV

---

## 1. Background — Why Threading Was Introduced

Loading a 2.7M block CSV file takes ~50 seconds. Running that on the Qt main thread freezes
the UI completely. The approach taken was:

1. Offload file I/O, centering, Morton sort, and bounds calculation to a worker thread via
   `QtConcurrent::run`.
2. Use `QMetaObject::invokeMethod` to post the finished model back to the main thread.
3. Transfer ownership across the thread boundary by moving a local `BlockModelSoA` into the
   lambda capture: `[model = std::move(localModel)]() mutable { ... }`.

---

## 2. Root Cause of the Crash

### 2.1 Lock Scope Too Wide

`ModelDiagnostics::modelMutex()` was introduced to prevent the render thread from reading
`m_model` while the main thread was writing to it. The lock was acquired correctly, but its
scope was too wide — it covered not just the data swap but also the Qt calls that followed:

```cpp
// main thread — invokeMethod lambda (BROKEN)
{
    std::lock_guard<std::mutex> lock(modelMutex()); // ← LOCKED

    m_model = std::move(model);
    m_provider->setModel(&m_model);    // calls markDirty()   ← Qt call inside lock
    emit availableFieldsChanged();     // signals delivered synchronously ← user code inside lock
    m_provider->setColorAttribute(…);  // calls markDirty() again         ← user code inside lock
}
```

`markDirty()` is inherited from `QQuick3DInstancing`. In Qt Quick 3D's basic (non-threaded)
render loop it can trigger a **synchronous** call to `getInstanceBuffer()` on the **same
thread** before the current call stack returns.

`getInstanceBuffer()` also tried to acquire the same mutex:

```cpp
// getInstanceBuffer — called synchronously on the SAME main thread
std::lock_guard<std::mutex> lock(modelMutex()); // ← mutex already held → UB
```

`std::mutex` is not recursive. Locking it a second time from the same thread is **undefined
behaviour**. On Windows/MinGW this produces a hard SEH exception (access violation or heap
corruption) that is not catchable by standard `catch(...)`, explaining why no exception
message appeared.

### 2.2 Why the Crash Appeared at `setModel`

```
setModel(&m_model)        ← prints "[MAIN] setModel: 0 ms"  ✓  (returns normally)
  └─ markDirty()          ← triggers synchronous getInstanceBuffer on basic render loop
       └─ lock(modelMutex) ← already held by this thread → UB → crash
```

The log printed because `setModel` itself returned before Qt processed the dirty flag. The
crash occurred in the re-entrant lock attempt triggered by the scene graph flush.

### 2.3 Why `shared_ptr` + Move Was Suspected but Is Not the Cause

```cpp
auto safeModel = std::make_shared<BlockModelSoA>(std::move(model));
```

`BlockModelSoA` contains only standard library containers (`std::vector`,
`std::unordered_map`), all of which have correct move constructors that transfer heap buffer
ownership without touching the data. The `shared_ptr` keeps the object alive until the lambda
exits. This is correct and safe. The crash was always the lock scope, not the move.

---

## 3. Fix Attempts

### 3.1 Attempt 1 — Move Signal Emissions Outside the Mutex Scope (unsuccessful)

```cpp
{
    std::lock_guard<std::mutex> lock(modelMutex());
    m_model = std::move(*safeModel);
    m_provider->setModel(&m_model);   // ← markDirty() still inside the lock
}
emit availableFieldsChanged();
m_provider->setColorAttribute(…);
```

`setModel` calls `markDirty()` while the lock is still held. The re-entrant path fired inside
`setModel` before the brace closed. Moving the signals out had no effect.

### 3.2 Attempt 2 — `std::recursive_mutex` (rejected — design smell)

Changing `std::mutex` to `std::recursive_mutex` allows the same thread to lock multiple times
without crashing. It was considered as a safety net but **rejected** for the following reasons,
following the guidance in *C++ Concurrency in Action* (Anthony Williams, ch. 3):

- **It masks a design flaw, not fixes it.** Re-entrancy happening at all means the lock scope
  is wrong. `recursive_mutex` silences the symptom while leaving the underlying ownership
  confusion in place.
- **"Never call user-supplied code while holding a lock"** (Williams, ch. 3.2.4). Qt signals
  and virtual Qt methods are user-supplied code — they can call back into anything, including
  code that tries to acquire the same lock. Holding a mutex across such calls makes the system's
  behaviour dependent on what those callbacks happen to do.
- **It removes a guarantee.** A `std::mutex` that can never be double-locked from the same
  thread is a strong invariant. `recursive_mutex` removes that guarantee, making future bugs
  invisible rather than loud.
- **It is not needed here.** The correct fix makes re-entrancy structurally impossible, so
  there is nothing for `recursive_mutex` to protect against.

### 3.3 Attempt 3 — Remove Threading Entirely (successful, temporary)

All threading infrastructure was removed. Everything ran on the main thread sequentially.
No mutex, no cross-thread handoff, `getInstanceBuffer` called only after the entire load
completed and the call stack had unwound.

**Result:** No crash.
**Trade-off:** UI froze for ~50 seconds. Accepted as a temporary stable baseline.

### 3.4 Attempt 4 — Narrow Lock Scope (correct fix, current implementation)

The fix applies two rules directly from *C++ Concurrency in Action* ch. 3:

> **"Lock the minimum data for the minimum time."**
> **"Never call user-supplied code while holding a lock."**

The mutex scope was reduced to cover only the `std::move` assignment — the one operation that
genuinely races with the render thread. All Qt calls (`setModel`, signals, `markDirty`) happen
after the lock is released:

```cpp
// invokeMethod lambda — main thread (CORRECT)
{
    std::lock_guard<std::mutex> lock(modelMutex());
    m_model           = std::move(model);   // ← only this line races with render thread
    m_modelRadius     = radius;
    m_availableFields = fields;
}   // ← mutex released here

// Mutex is free. If setModel → markDirty → getInstanceBuffer fires synchronously,
// getInstanceBuffer acquires the mutex without contention. No re-entrancy possible.
m_provider->setModel(&m_model);
emit availableFieldsChanged();
m_provider->setColorAttribute(m_availableFields.first());
```

`getInstanceBuffer` on the render thread (or synchronously on the main thread via the basic
render loop) acquires the same `std::mutex`. Because the main thread always releases the lock
before any Qt call, the render path never contends with a held lock from the same thread.

```
Worker thread           Main thread                     Render thread
─────────────           ──────────                      ─────────────
load → sort →           lock(mutex)                     [may block here]
bounds → fields           m_model = std::move(…)
                        unlock(mutex)                   lock(mutex) ← succeeds
                                                          getInstanceBuffer()
                        setModel(&m_model)              unlock(mutex)
                        emit signals
```

---

## 4. Summary

| Item | Outcome |
|---|---|
| Root cause | Lock scope too wide — `markDirty()` and Qt signals were called while holding `modelMutex`, triggering re-entrant `lock()` from the same thread → UB |
| `shared_ptr` + move semantics | Not the cause — semantically correct |
| Fix attempt 1 — signals outside lock | Failed — `setModel` still called `markDirty()` inside the lock |
| Fix attempt 2 — `recursive_mutex` | Rejected — masks a design flaw, violates Williams ch. 3 |
| Fix attempt 3 — single thread | Succeeded — removed the problem but froze the UI |
| Fix attempt 4 — narrow lock scope | **Correct fix** — lock covers only the data swap; all Qt calls happen after release |

---

## 5. Design Principle Applied

From *C++ Concurrency in Action*, Anthony Williams, Chapter 3 — Sharing data between threads:

- **Minimise lock scope** to the precise data that is shared and mutable. Do not hold a lock
  across any operation whose behaviour you do not fully control.
- **Never call user-supplied code while holding a lock.** Qt signals, virtual method calls, and
  callbacks are all user-supplied code in this sense.
- **Prefer `std::mutex` over `std::recursive_mutex`.** The latter should be a last resort for
  legacy interfaces. If you feel you need it, the lock scope is almost certainly wrong.
