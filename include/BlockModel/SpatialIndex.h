#pragma once

#include "BlockModel.h"
#include <memory>
#include <optional>

namespace Mining {

/**
 * @brief High-performance spatial index for the Block Model.
 * 
 * Uses CGAL's AABB Tree to provide O(log N) picking and spatial queries.
 * Bridges the gap between 3D geometric intersections and the SoA data model.
 */
class SpatialIndex {
public:
    SpatialIndex();
    ~SpatialIndex();

    // Move construction/assignment for background processing
    SpatialIndex(SpatialIndex&&) noexcept;
    SpatialIndex& operator=(SpatialIndex&&) noexcept;

    // Disable copy
    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;

    /**
     * @brief Build the index from a loaded BlockModelSoA.
     * @param model Reference to the model (must remain valid while index is used).
     */
    void build(const BlockModelSoA& model);

    /**
     * @brief Find the index of the block at a specific 3D coordinate.
     * @param query_x, query_y, query_z The normalized (local origin) coordinates.
     * @param tolerance Radius to search around the point (usually half block size).
     * @return The index in the SoA if found, std::nullopt otherwise.
     */
    std::optional<size_t> find_block_at(double query_x, double query_y, double query_z, double tolerance) const;

    /**
     * @brief Get the Bounding Box of the model.
     * Useful for setting camera center and clipping planes.
     */
    struct BBox {
        double x_min, y_min, z_min;
        double x_max, y_max, z_max;
    };
    BBox bounding_box() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace Mining
