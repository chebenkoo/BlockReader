#include "BlockModel/SpatialIndex.h"
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/squared_distance_3.h>

namespace Mining {

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef K::Point_3 Point;

/**
 * @brief Simple custom primitive for points in AABB tree.
 * Since CGAL 6.1.1 might have changed the point primitive header location,
 * we define a lightweight one ourselves.
 */
struct VoxelPrimitive {
public:
    typedef size_t Id;
    typedef Point Datum;
    typedef Point Point_reference;

    VoxelPrimitive(Id id, const Point& p) : m_id(id), m_p(p) {}

    const Datum& datum() const { return m_p; }
    Id id() const { return m_id; }
    const Point& reference_point() const { return m_p; }

private:
    Id m_id;
    Point m_p;
};

typedef CGAL::AABB_traits_3<K, VoxelPrimitive> Traits;
typedef CGAL::AABB_tree<Traits> Tree;

struct SpatialIndex::Impl {
    Tree tree;
    BBox bbox = {0,0,0,0,0,0};

    void clear() {
        tree.clear();
    }
};

SpatialIndex::SpatialIndex() : pimpl(std::make_unique<Impl>()) {}
SpatialIndex::~SpatialIndex() = default;

void SpatialIndex::build(const BlockModelSoA& model) {
    pimpl->clear();
    if (model.size() == 0) return;

    float x_min = model.x[0], x_max = model.x[0];
    float y_min = model.y[0], y_max = model.y[0];
    float z_min = model.z[0], z_max = model.z[0];

    for (size_t i = 0; i < model.size(); ++i) {
        Point p(model.x[i], model.y[i], model.z[i]); // float→double implicit, CGAL uses double kernel
        pimpl->tree.insert(VoxelPrimitive(i, p));

        x_min = std::min(x_min, model.x[i]); x_max = std::max(x_max, model.x[i]);
        y_min = std::min(y_min, model.y[i]); y_max = std::max(y_max, model.y[i]);
        z_min = std::min(z_min, model.z[i]); z_max = std::max(z_max, model.z[i]);
    }

    pimpl->bbox = {x_min, y_min, z_min, x_max, y_max, z_max}; // float→double for BBox members
    pimpl->tree.build();
}

std::optional<size_t> SpatialIndex::find_block_at(double query_x, double query_y, double query_z, double tolerance) const {
    if (pimpl->tree.empty()) return std::nullopt;

    Point query(query_x, query_y, query_z);
    
    // Find the closest point in the tree
    Point closest_point = pimpl->tree.closest_point(query);
    
    // Check if within tolerance (Euclidean distance squared)
    double dist_sq = CGAL::to_double(CGAL::squared_distance(query, closest_point));
    if (dist_sq > (tolerance * tolerance)) {
        return std::nullopt;
    }

    // Find the primitive (index) associated with this closest point
    auto primitive_id = pimpl->tree.closest_point_and_primitive(query).second;
    return primitive_id;
}

SpatialIndex::BBox SpatialIndex::bounding_box() const {
    return pimpl->bbox;
}

} // namespace Mining
