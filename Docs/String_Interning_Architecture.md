# Architecture Plan: String Interning (String Pool)

**Target Component:** `BlockModelSoA` / `Reader`
**Goal:** Reduce memory usage for categorical (text) attributes by ~90% and prevent `std::bad_alloc` crashes.

---

## 1. The Problem: String Overhead
For a model with 2.7 million blocks and 80+ attributes, storing text using standard methods is unsustainable:
*   `std::string` has a **32-byte overhead** per instance (on 64-bit systems).
*   2.7M blocks * 32 bytes = **~86 MB per column** just for the empty containers.
*   80 columns * 86 MB = **~6.8 GB RAM**, leading to immediate crashes regardless of actual text content.

## 2. The Solution: String Interning
Most mining data columns (e.g., "RockType", "Status", "Bench") contain highly repetitive values. Instead of storing the string millions of times, we store it once.

### Data Structure: `InternedString`
Instead of `std::vector<std::string>`, we will use:
1.  **Unique Pool:** A `std::vector<std::string>` containing one copy of every unique value found in that column.
2.  **Indices:** A `std::vector<int32_t>` (4 bytes per block) containing the index into the Unique Pool.
3.  **Lookup Map:** A temporary `std::unordered_map<std::string, int32_t>` used *only during loading* to quickly find existing IDs.

### Memory Comparison (Per Column @ 2.7M blocks)
| Method | Per Block | Total (Base) | Total (80 cols) | Risk |
|---|---|---|---|---|
| `std::string` | ~40-64 bytes | ~150 MB | ~12.0 GB | **CRASH** |
| `char[16]` | 16 bytes | ~43 MB | ~3.4 GB | Data loss / High RAM |
| **Interning** | **4 bytes** | **~11 MB** | **~0.8 GB** | **SAFE** |

---

## 3. Robust Type Probing
To prevent the "Memory Trap" where numeric columns are misclassified as strings:
1.  **Skip Whitespace/Empty:** The probe will ignore empty cells. A column is only `STRING` if it contains at least one non-numeric, non-empty value.
2.  **Majority Vote:** Sample the first 100 rows. If >95% are numeric, treat as `FLOAT`.

---

## 4. Implementation Steps

### Phase 1: `BlockModel.h`
*   Define `struct InternedString`.
*   Update `BlockModelSoA` to use `unordered_map<string, InternedString> string_attributes`.
*   Implement `shrink_to_fit()` to clear the temporary `Lookup Map` after loading.

### Phase 2: `Reader.cpp`
*   Refactor the CSV loop to use `InternedString::add(std::string_view)`.
*   This ensures zero allocations for repetitive strings during the main parse loop.

### Phase 3: `Instancing.cpp`
*   Update `getBlockInfo` to resolve the integer ID back to a string from the pool when the user selects a block.
*   Update `getInstanceBuffer` to handle categorical coloring (assigning unique colors to unique string IDs).

---

## 5. Summary of Benefits
1.  **Stability:** 2.7M blocks with 80 attributes will fit comfortably in ~1.5 GB total RAM (including spatial data).
2.  **Performance:** Loading is faster due to fewer heap allocations.
3.  **Features:** Allows the Digital Twin to display rich categorical data (e.g., "Ore Type: Oxide Gold") without crashing.
