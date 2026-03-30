# BlockReader — Fix & Feature Log

## Session Summary (March 2026)

---

### 1. Root-cause diagnosis: 20 GB memory spike during CSV parse

**Problem:** Loading a 2.7 M-block CSV with 60+ attributes caused 20–22 GB RAM usage.

**Diagnosis path:**
- Added per-structure checkpoint logging every 500 K rows (`spatial`, `numeric`, `intern_idx`, `lookup_maps`, `raw_str`, `UNACCOUNTED`).
- Used Visual Studio Native Memory Profiler snapshot comparison.
- Profiler showed **live heap ~925 MB** (`float[]` × 76 vectors), while Task Manager showed 22 GB.
- Conclusion: the 22 GB were **committed-but-freed virtual memory pages** left in the Windows Working Set after vector doublings during parsing — not a memory leak.

**Root causes identified and fixed:**

| # | Cause | Fix |
|---|---|---|
| A | `model.reserve(N)` on 60+ vectors simultaneously — Windows OS prefetch brings all pages into RAM at once | Removed upfront reserve; vectors grow by doubling |
| B | `std::string s(val)` in intern hot-path — 2.7 M heap allocs per string column per parse | Replaced with C++20 transparent hash so `find(string_view)` allocates zero bytes on cache hit |
| C | Freed pages stay in Working Set after parse ends | Added `HeapCompact` + `SetProcessWorkingSetSize(-1,-1)` after `shrink_to_fit()` |

**Result:** Peak process memory went from **18+ GB to 383 MB** for 880 K-block file.

---

### 2. String interning architecture

**Problem:** Categorical columns (e.g. RockType, OreCode) stored as `std::vector<std::string>` used 16–32 bytes per block per column.

**Solution (BlockModel.h):**
- `InternedString`: per-block `int32_t` indices (4 bytes each) + a small pool of unique string values.
- `local_intern_maps` built during parse, moved to the pool via `map.extract()` — zero copies (C++17).
- High-cardinality columns (>50% unique values in 100-row probe) classified as `RAW_STRING` and skip interning.
- Memory: **4 bytes/block** + unique pool vs. 16–32 bytes/block previously.

---

### 3. MSVC build support

**Problem:** Project only compiled with MinGW/GCC. MSVC produced errors C2062 and C3878.

**Root cause:** `ModelDiagnostics.h` included `<windows.h>` without `NOMINMAX`. The `min`/`max` macros Windows defines expanded inside `std::min(...)` and `std::numeric_limits<T>::max()` calls, producing invalid token sequences.

**Fixes:**
- Added `#define NOMINMAX` before `<windows.h>` in `ModelDiagnostics.h`.
- Added to `CMakeLists.txt` for all MSVC builds: `NOMINMAX`, `/utf-8`, `/MP`, `/wd4267`, `/wd4244`.

---

### 4. Qt Quick Controls style warnings eliminated

**Problem:** 83 style warnings fired on every load — the Windows native Qt style forbids `background: Rectangle {}` overrides on TextField/Button.

**Fix:** Set Fusion style before `QGuiApplication` in `main.cpp`:
```cpp
qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
```

---

### 5. Main-thread lambda blocked — model never reached renderer

**Problem:** After the worker thread posted its lambda via `QMetaObject::invokeMethod` (confirmed SUCCESS), no `[MAIN]` logs appeared and the 3D model never rendered.

**Root cause:** Inside the lambda, `emit availableFieldsChanged()` fired while status was still "Computing Attribute Ranges...". The reactive QML binding:
```qml
visible: availableFields.length > 0 && !status.includes("Loaded")
```
re-opened the mapping dialog. `onVisibleChanged` then synchronously created **83 rows x 83-item ComboBoxes = 6,889 QML objects** on the main thread, blocking the lambda for several seconds and starving the render loop.

**Fix (Main.qml):**
- Removed the reactive `visible` binding from the dialog.
- Added a `Connections` handler that opens the dialog only when `!isLoading`:
```qml
Connections {
    target: modelController
    function onAvailableFieldsChanged() {
        if (modelController.availableFields.length > 0 && !modelController.isLoading)
            mappingDialog.open()
    }
}
```
- Reordered button handler: `close()` before `loadWithMapping()` so `isLoading = true` is set before any subsequent `availableFieldsChanged` can fire.

---

### 6. String field coloring (new feature)

**Problem:** The "Color By" dropdown listed all fields, but selecting a categorical string field had no effect — `getInstanceBuffer()` only looked in `m_model->attributes` (numeric floats).

**Implementation (Instancing.cpp):**
- `getInstanceBuffer()` now checks `m_model->string_attributes` when the colour attribute is not found in numeric attributes.
- Each unique category maps to an evenly-spaced hue on the full HSV wheel using the same 6-sector fast math as the numeric path: `hue = categoryIndex / uniqueCategoryCount`.
- `setColorAttribute()` sets `m_minRange = 0`, `m_maxRange = uniqueCount - 1` for string attributes so the range property stays consistent.

**Result:** Selecting a categorical field (e.g. RockType, OreCode, mat_sch) renders each category in a visually distinct colour. Numeric fields continue to use the blue-green-red gradient.

---

### Memory profile comparison

| Stage | Before fixes | After fixes |
|---|---|---|
| Peak during parse (2.7 M blocks) | ~22,000 MB | ~1,867 MB |
| After Working Set trim | — | 6 MB |
| Worker thread done | ~18,400 MB | 383 MB |
| Live heap (VS profiler) | ~925 MB | ~925 MB (always correct) |

The live heap was always correct. The 22 GB was entirely committed-but-freed OS pages — the Working Set trim now reclaims them immediately after parse.
