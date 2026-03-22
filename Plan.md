# Tactical Optimizer: High-Performance Mine Planning & Scheduling

Build a C++/Qt/QML system with modern optimization that is mathematically powerful and operationally intuitive. Outperform Deswik (fragmented), Alastri/Spry (manual), and MiningMath (black box) by delivering real-time, physics-aware optimization.

## Executive Strategy

- **Philosophy:** Optimization that respects physics.
- **Core Differentiator:** Real-time feedback — change a constraint (e.g., "Max Trucks") and immediately see Geometry (Pit Shell) and Schedule (NPV) update.
- **Tech Stack:** C++20, Qt 6/QML, Google OR-Tools, CGAL/OpenMesh, Qt Quick 3D, `fast_float`, `fmt`.

---

## Phase 1: Digital Twin Engine (Months 1–4)

- **Goal:** Render and query 10M+ blocks at 60 FPS; instant trust through responsiveness.

| Component        | Problem                                  | Algorithm / Method                          | Why It Beats Competitors                                                                               |
|------------------|-------------------------------------------|---------------------------------------------|---------------------------------------------------------------------------------------------------------|
| Data Structure   | OOP pointers cause CPU cache misses       | Data-Oriented Design (DOD) + Morton (Z)     | Load 5GB models in <2s; query 50x faster than legacy OOP approaches used by Alastri/Spry                |
| Rendering        | Millions of cubes choke standard engines  | GPU Instancing (Qt Quick 3D)                | Reuse one cube mesh 5M times with unique transforms; near-zero CPU overhead on draw calls               |
| I/O              | Text parsing (CSV) bottlenecks            | Memory-mapped files + `fast_float`          | Instant project opening; competitors often take minutes on massive datasets                             |
| State Management | Scheduling mutates state millions of times| Delta Layer (BitSet separate from Base Model)| Solver only touches tiny hot memory layer for Mined/Unmined state; zero cache misses on read-only base |

- **Recommended Reading:** "Data-Oriented Design" — Richard Fabian.
- **Libraries:** `fast_float`, `fmt`, Qt Quick 3D.

---

## Phase 2: Geometric Intelligence (Months 5–8)

- **Goal:** Generate the ultimate pit and diggable mining shapes instantly.

| Component        | Problem                                 | Algorithm / Method                                      | Why It Beats Competitors                                                                                              |
|------------------|------------------------------------------|---------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------|
| Proxy Haulage    | Pit shell depends on haulage costs       | Centroid-to-Ramp-Exit (Euclidean/Manhattan)             | Provides 90% accurate costs for pit optimization (haulage is ~50% of cost) without waiting for full simulation         |
| Pit Optimization | Find most profitable pit shell           | Hochbaum Pseudoflow (Network Flow)                      | Deswik includes this as a separate step; here it runs continuously — pit expands/shrinks live with price changes      |
| Aggregation      | Solvers can’t schedule single blocks     | Agglomerative Clustering + Connectivity Constraints     | Creates rectilinear "dig-lines" (not blobs) that respect bench/face adjacency; guarantees physically excavatable shapes|
| Boolean Ops      | Accurate reserves (Ore vs. Pit)          | AABB Trees (CGAL) + Ray Casting                         | Exact geometric slicing — precise partial blocks, not estimates                                                        |

- **Recommended Reading:** "Computational Geometry: Algorithms and Applications" — Berg et al.
- **Key Algorithm:** Hochbaum’s Pseudoflow.

---

## Phase 3: Tactical Scheduler (Months 9–15)

- **Goal:** Maximize NPV with a solver that "sees" both short-term precision and long-term value.

| Component       | Problem                               | Algorithm / Method                     | Why It Beats Competitors                                                                                                             |
|-----------------|----------------------------------------|----------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------|
| The Schedule    | Maximize NPV over time                 | Mixed-Integer Programming (MIP)        | Alastri/Spry rely on heuristics; MIP provides provable optimality for tactical decisions                                             |
| The Horizon     | 20-year horizon too big for full MIP   | Rolling Horizon: MIP (Y1–2) + LP (Y3–20) | Precision now (binary decisions) and speed later (LP) — focus compute where it matters                                              |
| Strategic Link  | Short-term choices hurt long-term value | Duality (Shadow Prices)                | Use LP duals to price boundary blocks in short-term MIP — the solver effectively "sees the future"                                    |
| Infeasibility   | Conflicting user constraints (Grade vs. Time) | IIS Detection (Irreducible Inconsistent Subsystem) | Instead of generic "Error", explicitly tells user: "Cannot hit Grade Target because Crusher Capacity is insufficient." |

- **Recommended Reading:** "Model Building in Mathematical Programming" — H. Paul Williams.
- **Library:** Google OR-Tools (C++ wrapper).

---

## Phase 4: Operational Reality (Months 16+)

- **Goal:** Ban physically impossible plans; ensure reachability and realistic haulage.

| Component     | Problem                                        | Algorithm / Method                     | Why It Beats Competitors                                                                                                      |
|---------------|-------------------------------------------------|----------------------------------------|------------------------------------------------------------------------------------------------------------------------------|
| Reachability  | Prevent mining blocks trucks cannot reach       | Implicit Graph BFS                     | Deswik/Alastri can schedule impossible blocks; BFS pre-prunes unreachable blocks (e.g., after ramp removal)                   |
| Haulage       | Accurate cycle times                           | Detailed Simulation (Dijkstra on Road Network) | Validates schedule with physics-based reality after the fast "Proxy Haulage" used in optimization                             |

- **Recommended Reading:** "Introduction to Algorithms" (CLRS) — Graph Theory chapters.

---

## Competitive Analysis: How You Win

| Feature      | Deswik                                    | Alastri / Spry                             | Your Solution (Tactical Optimizer)                                                             |
|--------------|--------------------------------------------|--------------------------------------------|-------------------------------------------------------------------------------------------------|
| Math Engine  | Strong (Pseudoflow), separate modules      | Weak (Heuristic/Manual), "Gantt + 3D"      | Hybrid: MIP precision + heuristics for speed; fully integrated                                  |
| Workflow     | Import/Export across modules (CAD $\leftrightarrow$ Sched) | Fast setup, manual iteration loop            | Unified: change a constraint, see the result — no exports                                       |
| Usability    | High learning curve ("Expert Tool")        | Intuitive, visual                           | Game-like: Qt Quick 3D with Digital Twin responsiveness                                         |
| Physics      | Can schedule impossible mining              | User must manually link tasks              | Reachability solver: automatically bans physically impossible tasks                              |

---

## MVP: The "Killer" Feature Set

1. **Load 5M Block Model + Topography** (DOD + Memory Map).
2. **Define Economic Parameters + Auto-Calculate Proxy Haulage Cost.**
3. **Auto-Generate Pit Shell** (Pseudoflow).
4. **Auto-Cluster into Mining Shapes** (Agglomerative Clustering for "Dig-Lines").
5. **Optimize:**
   - **Pre-Check:** Reachability (BFS).
   - **Solve:** Rolling Horizon (MIP Y1–2, LP Y3–20) in OR-Tools.
   - **Fail-Safe:** If Infeasible -> Auto-Diagnose with IIS.
6. **Output:** Animated 3D mine-life sequence + "Bottleneck Analysis" Dashboard.

---

## Focus First

If visualization isn’t fast, the math doesn’t matter. Nail Phase 1 to establish trust and usability before scaling optimization complexity.
