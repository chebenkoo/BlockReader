# Mining Engine: Spatial Data Pipeline

This document defines the data flow from raw disk files to interactive 3D mining models.

## 1. Data Ingestion & Pre-Scanning
*   **Source:** `.dat` (Micromine Binary) or `.csv` (Text).
*   **Step A (Pre-Scan):** `MicromineReader::get_variables` or CSV header extraction.
    - Discover available fields (e.g., `AuCut`, `DENSITY`, `WEATH`).
    - Expose fields to UI for user-driven mapping.
*   **Step B (Mapping):** User assigns columns to `X, Y, Z` and dynamic attributes.
*   **Step C (Load):** `MicromineReader::load` parses the full dataset into `BlockModelSoA`.

## 2. Spatial Normalization (The Framework)
*   **Global to Local:** `MicromineReader::center_model` calculates the model centroid.
*   **Transformation:** Subtracts centroid from raw coordinates to move the model to `(0,0,0)`.
*   **Why:** Prevents "Floating Point Jitter" in the GPU by ensuring all coordinates fit within high-precision 32-bit float ranges.

## 3. Spatial Indexing & Locality
*   **Grid Arithmetic:** `i, j, k` indices calculated via block-size division.
*   **Morton Order (Z-Curve):** `SpatialLocality::encode_morton_3d` interleaves index bits into a 64-bit key.
*   **Data Sorting:** `sort_by_locality` reorders SoA vectors by Morton key to maximize CPU cache hits during Phase 2 (Pit Optimization).
*   **CGAL AABB Tree:** `SpatialIndex::build` constructs a geometry tree for $O(\log N)$ spatial queries.

## 4. Visual Pipeline (GPU Instancing)
*   **Provider:** `BlockModelProvider` (inherits `QQuick3DInstancing`).
*   **Filtering:** 
    - **Grade Cutoff:** Only blocks above `minGrade` are written to the GPU buffer.
    - **Visibility:** `visible` bitset in SoA allows instant hiding/showing.
*   **Draw Call:** Qt Quick 3D renders 10M+ voxels in a single draw call using the generated instance buffer.

## 5. Interaction (Picking)
*   **The Query:** Camera Ray (World) → `WorldToModelMatrix`.
*   **Intersection:** `SpatialIndex::find_block_at` queries the **CGAL AABB Tree**.
*   **Lookup:** Tree returns the `Handle` (index) → `BlockModelSoA` retrieves field values (Grade, RockType).

---

### Implementation Mapping

| Conceptual Flow | C++ Class / Method |
| :--- | :--- |
| **Ingestion** | `MicromineReader.cpp` / `Reader.cpp` |
| **Framework** | `center_model()` |
| **Indexing** | `CGAL::AABB_tree<VoxelPrimitive>` |
| **Locality** | `SpatialLocality::encode_morton_3d` |
| **Rendering** | `BlockModelProvider::getInstanceBuffer` |
| **Picking** | `SpatialIndex::find_block_at` |
