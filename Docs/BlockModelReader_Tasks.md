# Block Model Reader: Development Tasks

This document outlines the detailed tasks for implementing a high-performance, voxel-based block model reader.

## Phase 1: Data Ingestion & Memory Management
- [ ] **1.1 Define Data-Oriented (DOD) Block Model Structure**
  - Implement SoA (Structure of Arrays) for `BlockModel` (X, Y, Z, Grade, RockType, MinedState).
  - Use `std::vector` or memory-mapped buffers for millions of entries.
  - Implement Morton Order (Z-curve) indexing for cache-friendly spatial access.
- [ ] **1.2 High-Performance CSV/DAT Parser**
  - Integrate `fast_float` for rapid string-to-float conversion.
  - Implement multi-threaded parsing (using `std::jthread` or `QtConcurrent`).
  - Add validation logic for Null/NA values in CSV columns.
- [ ] **1.3 Spatial Indexing (CGAL)**
  - Integrate `CGAL::AABB_tree` for fast intersection and distance queries.
  - Wrap model centroids into `CGAL::Point_3` objects.
  - Implement Bounding Box calculation for camera focus and view frustum culling.

## Phase 2: Qt Quick 3D Visualization
- [ ] **2.1 Bridge C++ Model to QML**
  - Create `BlockModelProvider` (inheriting from `QQuick3DInstancing`).
  - Map model attributes (e.g., Grade) to instance data buffers.
- [ ] **2.2 GPU Instanced Rendering**
  - Setup `Model` in QML using a single cube mesh with `instancing` property.
  - Implement Vertex Shader for dynamic color mapping (LUT) based on Grade.
- [ ] **2.3 Dynamic Filtering & Querying**
  - Implement "Active Layer" BitSet for fast visibility filtering (e.g., `Grade > 0.5`).
  - Enable interactive block picking (Ray casting using CGAL AABB Tree).

## Phase 3: Validation & Optimization
- [ ] **3.1 Unit Testing**
  - Verify parser accuracy against gold-standard CSV files.
  - Benchmark loading times for 1M, 5M, and 10M block datasets.
- [ ] **3.2 Memory Profiling**
  - Use Valgrind/Heaptrack to minimize memory footprint.
  - Ensure zero copies during data ingestion (Move semantics).
- [ ] **3.3 UI Feedback**
  - Add progress bars and "Project Stats" dashboard in QML.
