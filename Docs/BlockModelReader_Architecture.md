# Block Model Reader: Architecture Overview

This diagram represents the high-performance architecture for reading, indexing, and rendering massive block models (10M+ voxels).

## System Architecture (Mermaid)

```mermaid
graph TD
    subgraph Data Layer
        CSV[CSV/DAT Files] --> Parser[Fast CSV Parser (fast_float)]
        Parser --> BlockModel[Block Model (SoA/DOD)]
        BlockModel --> Morton[Morton Order (Z-curve) Index]
    end

    subgraph Spatial Layer
        BlockModel --> CGAL_AABB[CGAL AABB Tree (Point_3)]
        CGAL_AABB --> Intersection[Fast Picking/Slicing]
        CGAL_AABB --> BoundingBox[Model Bounding Box]
    end

    subgraph View Layer (Qt)
        BlockModel --> InstancingProvider[BlockModelProvider (QQuick3DInstancing)]
        InstancingProvider --> GPU_Buffer[GPU Instance Buffer (TBO/SSBO)]
        GPU_Buffer --> QtQuick3D[Qt Quick 3D Engine]
        QtQuick3D --> CubeMesh[Instanced Cube Mesh]
        ColorMap[Grade LUT (Vertex Shader)] --> CubeMesh
    end

    subgraph Logic/Filtering
        UI_Filter[UI Filtering Constraints] --> BitSet[Active Layer BitSet]
        BitSet --> InstancingProvider
    end
```

## Component Descriptions

| Component | Responsibility | Technical Choice |
| :--- | :--- | :--- |
| **Data Layer** | Fast ingestion and cache-efficient storage. | SoA (Structure of Arrays) & `fast_float`. |
| **Spatial Layer** | High-speed geometric queries. | `CGAL::AABB_tree<Point_3>` for $O(\log n)$ performance. |
| **View Layer** | 60 FPS rendering of 10M+ blocks. | `QQuick3DInstancing` for hardware-accelerated instancing. |
| **Filtering Layer** | Real-time hiding/showing of blocks. | `std::bitset` or custom BitSet for ultra-fast "is-active" checks. |
