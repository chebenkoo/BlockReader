# Tactical Optimizer: Mining Schedule Engine

A high-performance C++/Qt 6 engine for mine planning and scheduling, utilizing Data-Oriented Design (SoA) and CGAL spatial indexing.

## Prerequisites

- **C++20 Compiler** (GCC 11+, Clang 13+, or MSVC 2022+)
- **Qt 6.8+** (Quick, Quick3D, Core)
- **Boost 1.90.0** (installed at `C:/boost_1_90_0`)
- **CGAL 6.1.1** (included in the repository)

## Build Instructions

1. **Configure the project:**
   Ensure your environment variables for Qt are set, then run CMake from the project root:
   ```bash
   mkdir build
   cd build
   cmake .. -G "MinGW Makefiles" # Or "Visual Studio 17 2022"
   ```

2. **Compile:**
   ```bash
   cmake --build . --parallel
   ```

$env:PATH = "C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH

$env:PATH = "C:\Qt\6.10.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH; .\appMiningSchedule.exe

 $env:PATH = "C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH; cmake --build . --parallel
3. **Run:**
   ```bash
   ./appMiningSchedule
   ```

## Project Structure

- `include/BlockModel`: Core data structures (SoA) and Parser interfaces.
- `src/BlockModel`: Implementation of the high-speed CSV/DAT reader.
- `Docs/`: Architecture diagrams and detailed task lists.
- `third_party/`: Header-only libraries like `fast_float.h`.

## Technical Highlights

- **Fast Ingestion:** Uses `fast_float` for $10\times$ faster CSV parsing.
- **Cache Locality:** Implements Morton Order (Z-curve) for $O(1)$ spatial data sorting.
- **Precision:** Coordinate normalization (Local Origin) to prevent 32-bit GPU jitter.
- **Scalability:** Designed for 10M+ voxels using Qt Quick 3D Instanced Rendering.
