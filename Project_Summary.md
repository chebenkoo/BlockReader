# Block Mining Model Renderer - Architecture & Code Summary

## Overview
This application is a 3D visualization tool for mining block models, built using **C++**, **Qt 6**, and **Qt Quick 3D**. It allows users to load block model datasets from CSV or Micromine (`.dat`) files, map specific data columns to physical 3D properties and custom attributes, and efficiently render potentially massive models in a 3D interactive viewport.

## Core Architecture

### 1. Frontend: QML UI (`Main.qml`)
The user interface is declarative, built with Qt Quick and Qt Quick 3D.
- **File & Field Mapping:** Exposes a `FileDialog` to load files. Once loaded, a `mappingDialog` allows the user to specify which columns from the dataset represent coordinates (`X, Y, Z`), dimensions (`XSPAN, YSPAN, ZSPAN`), and custom attribute properties (e.g., Ore Grade, Materials).
- **3D Scene:** Utilizes Qt Quick 3D features, placing a primary camera (`PerspectiveCamera`) linked with an `OrbitCameraController` for navigation.
- **Rendering Interface:** The rendering relies on a `#Cube` standard mesh, handled via `QQuick3DInstancing` (exposed from C++ as `BlockModelProvider`) for highly performant batched rendering of thousands/millions of individual block models.
- **Interactivity:** Provides UI controls (sliders/comboboxes) for block size scaling, minimum grade threshold cutoffs, and property-based color coding.

### 2. Backend Logic (`main.cpp` & `ModelController`)
The C++ backend manages data states and application lifecycle.
- Hosts the `ModelController`.
- Acts as the bridge between the UI mapping arrays and the internal memory model. It processes the mappings like `EAST -> X`, `NORTH -> Y`, `RL -> Z` and directs the underlying readers to ingest the file accordingly.

### 3. Data Ingestion & Storage (`Reader.cpp` & `MicromineReader.cpp`)
- **Parsers:** Supports standard CSV parsing and binary Micromine `.dat` interactions.
- **Struct-of-Arrays (SoA):** The imported dataset is loaded into a `BlockModelSoA` data structure. Instead of using generic arrays of block objects, properties (X, Y, Z, Spans, Attributes) are held in individual contiguous vectors (`std::vector`). This heavily optimizes CPU cache efficiency during rendering passes.
- **Centering:** Often, block models use massive true geographic coordinates. `MicromineReader::center_model` translates the parsed coordinates closer to the origin `(0,0,0)` to ensure smooth float-precision rendering in the 3D space.

### 4. Instanced Rendering (`src/BlockModel/Instancing.cpp`)
The core renderer component mapping the C++ structs to the GPU.
- Implement Qt's `QQuick3DInstancing` API (`BlockModelProvider`).
- **Coordinate Conversion:** Mining coordinate systems are usually Z-up (RL). Qt Quick 3D defaults to Y-up. The renderer adapts this natively during translation (`position = QVector3D(x, z, -y)`).
- **Transformation:** Generates transformation matrices based on defined dimensions (`sx, sy, sz`). Standard blocks base off Qt's `100x100x100` unit `#Cube`, with scale being manipulated by `(span / 100.0f) * block_size`.
- **Filtering & Coloring:** Iterates through every block, assigning an exact position, scale, and color based on the selected `colorAttribute`. Implements logical filters like excluding blocks that fall below a minimum geological grade `m_minGrade` before rendering.

## Typical Data Flow
1. User selects `file.csv`.
2. Controller parses file headers and presents a mapping UI.
3. User maps `_EAST` to X, `_NORTH` to Y, `_RL` to Z, and adds attributes like `ce2o3` (grade).
4. `Reader` iterates over the rows parsing them strictly into the `BlockModelSoA` column vectors.
5. The dataset is offset to center around the zero-origin.
6. The `BlockModelProvider` builds a large GPU Instance Buffer assigning every block a position, scale, and color based on the `ce2o3` attribute limits.
7. Qt Quick 3D batches and renders the grid using massive performance gains achievable via hardware instancing.
