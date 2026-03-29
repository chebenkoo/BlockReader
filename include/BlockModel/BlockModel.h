#pragma once

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <utility>

namespace Mining {

// ---------------------------------------------------------------------------
// SpatialLocality – Morton (Z-order) encoding
// ---------------------------------------------------------------------------
struct SpatialLocality
{
    static uint64_t spread_bits(uint32_t v)
    {
        uint64_t x = v & 0x1fffff;
        x = (x | x << 32) & 0x1f00000000ffff;
        x = (x | x << 16) & 0x1f0000ff0000ff;
        x = (x | x <<  8) & 0x100f00f00f00f00f;
        x = (x | x <<  4) & 0x10c30c30c30c30c3;
        x = (x | x <<  2) & 0x1249249249249249;
        return x;
    }

    static uint64_t encode_morton_3d(uint32_t x, uint32_t y, uint32_t z)
    {
        return spread_bits(x) | (spread_bits(y) << 1) | (spread_bits(z) << 2);
    }
};

// ---------------------------------------------------------------------------
// Mat4 – minimal 4x4 column-major matrix for coordinate transforms
// ---------------------------------------------------------------------------
struct Mat4
{
    double m[16] = {};   // column-major

    static Mat4 identity()
    {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
        return r;
    }

    static Mat4 translation(double tx, double ty, double tz)
    {
        Mat4 r = identity();
        r.m[12] = tx; r.m[13] = ty; r.m[14] = tz;
        return r;
    }

    static Mat4 rotation_z(double deg)
    {
        double rad = deg * (3.14159265358979323846 / 180.0);
        Mat4 r = identity();
        r.m[0] =  std::cos(rad); r.m[4] = -std::sin(rad);
        r.m[1] =  std::sin(rad); r.m[5] =  std::cos(rad);
        return r;
    }

    static Mat4 rotation_x(double deg)
    {
        double rad = deg * (3.14159265358979323846 / 180.0);
        Mat4 r = identity();
        r.m[5] =  std::cos(rad); r.m[9] = -std::sin(rad);
        r.m[6] =  std::sin(rad); r.m[10] =  std::cos(rad);
        return r;
    }

    static Mat4 rotation_y(double deg)
    {
        double rad = deg * (3.14159265358979323846 / 180.0);
        Mat4 r = identity();
        r.m[0] =  std::cos(rad); r.m[8] =  std::sin(rad);
        r.m[2] = -std::sin(rad); r.m[10] =  std::cos(rad);
        return r;
    }

    Mat4 operator*(const Mat4& b) const
    {
        Mat4 c;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
            {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += m[k * 4 + row] * b.m[col * 4 + k];
                c.m[col * 4 + row] = s;
            }
        return c;
    }

    void transform_point(double& x, double& y, double& z) const
    {
        double ox = m[0]*x + m[4]*y + m[8]*z  + m[12];
        double oy = m[1]*x + m[5]*y + m[9]*z  + m[13];
        double oz = m[2]*x + m[6]*y + m[10]*z + m[14];
        x = ox; y = oy; z = oz;
    }
};

// ---------------------------------------------------------------------------
// Voxel – Array-of-Structures (AoS) representation
// ---------------------------------------------------------------------------
struct Voxel {
    float x, y, z;          // Centroid
    float xs, ys, zs;       // Dimensions
    int32_t i, j, k;        // Grid indices
    uint8_t mined_state;
    uint8_t visible;
    uint64_t morton_key;
    // Note: dynamic attributes in AoS are often stored separately 
    // to maintain a fixed-size struct for memory efficiency.
};

struct BlockModelAoS {
    std::vector<Voxel> voxels;
    std::unordered_map<std::string, std::vector<float>> attributes;

    size_t size() const { return voxels.size(); }
    bool   empty() const { return voxels.empty(); }
    void   clear() { voxels.clear(); attributes.clear(); }
};

// ---------------------------------------------------------------------------
// BlockModelSoA – Structure-of-Arrays block model storage
// ---------------------------------------------------------------------------
struct BlockModelSoA
{
    std::vector<float> x, y, z;           // centered coords — float is sufficient post-centering
    std::vector<float> x_span, y_span, z_span;
    std::vector<int>    i, j, k;
    std::vector<uint8_t> mined_state;
    std::vector<uint8_t> visible;
    std::vector<uint64_t> morton_key;
    
    // Numeric attributes (4 bytes per block)
    std::unordered_map<std::string, std::vector<float>>       attributes;
    
    // Categorical attributes (Interned: 4 bytes per block + unique pool)
    struct InternedString {
        std::vector<int32_t>      indices; // index into unique_values
        std::vector<std::string>  unique_values;

        const std::string& get(size_t block_idx) const {
            static const std::string empty = "";
            if (block_idx >= indices.size()) return empty;
            int32_t val_idx = indices[block_idx];
            return (val_idx >= 0 && val_idx < (int32_t)unique_values.size()) ? unique_values[val_idx] : empty;
        }

        void reserve(size_t n) { indices.reserve(n); }
        
        void shrink_to_fit() { 
            indices.shrink_to_fit(); 
            unique_values.shrink_to_fit();
        }
        
        void clear() { indices.clear(); unique_values.clear(); }
        
        size_t memory_usage() const {
            size_t total = indices.capacity() * sizeof(int32_t);
            total += unique_values.capacity() * sizeof(std::string);
            for (const auto& s : unique_values) total += s.capacity();
            return total;
        }
    };

    std::unordered_map<std::string, InternedString>          string_attributes;
    std::unordered_map<std::string, std::pair<float, float>>  attribute_ranges;

    void add_attribute(const std::string& name)
    {
        if (attributes.find(name) == attributes.end()) {
            attributes.emplace(name, std::vector<float>{});
            attribute_ranges[name] = {0.0f, 0.0f};
        }
    }

    void add_string_attribute(const std::string& name)
    {
        if (string_attributes.find(name) == string_attributes.end())
            string_attributes[name] = InternedString{};
    }

    size_t size() const { return x.size(); }
    bool   empty() const { return x.empty(); }

    void clear() {
        x.clear(); y.clear(); z.clear();
        x_span.clear(); y_span.clear(); z_span.clear();
        i.clear(); j.clear(); k.clear();
        mined_state.clear(); visible.clear(); morton_key.clear();
        attributes.clear();
        string_attributes.clear();
        attribute_ranges.clear();
    }

    void reserve(size_t n) {
        x.reserve(n); y.reserve(n); z.reserve(n);
        x_span.reserve(n); y_span.reserve(n); z_span.reserve(n);
        i.reserve(n); j.reserve(n); k.reserve(n);
        mined_state.reserve(n); visible.reserve(n); morton_key.reserve(n);
        for (auto& [_, vec] : attributes) vec.reserve(n);
        for (auto& [_, vec] : string_attributes) vec.reserve(n);
    }

    void shrink_to_fit() {
        x.shrink_to_fit(); y.shrink_to_fit(); z.shrink_to_fit();
        x_span.shrink_to_fit(); y_span.shrink_to_fit(); z_span.shrink_to_fit();
        i.shrink_to_fit(); j.shrink_to_fit(); k.shrink_to_fit();
        mined_state.shrink_to_fit(); visible.shrink_to_fit(); morton_key.shrink_to_fit();
        for (auto& [_, vec] : attributes) vec.shrink_to_fit();
        for (auto& [_, vec] : string_attributes) vec.shrink_to_fit();
    }

    // Sort all parallel arrays by Morton key for cache-friendly memory layout.
    // Spatially adjacent blocks end up adjacent in memory, improving GPU/CPU
    // cache hit rate when building the instance buffer.
    void sort_by_morton() {
        const size_t n = x.size();
        if (n == 0) return;

        // Build index permutation sorted by Morton key
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return morton_key[a] < morton_key[b];
        });

        // Apply the same permutation to every parallel array
        auto apply = [&](auto& vec) {
            using T = typename std::decay_t<decltype(vec)>::value_type;
            std::vector<T> tmp(n);
            for (size_t idx = 0; idx < n; ++idx) tmp[idx] = std::move(vec[order[idx]]);
            vec = std::move(tmp);
        };

        apply(x); apply(y); apply(z);
        apply(x_span); apply(y_span); apply(z_span);
        apply(i); apply(j); apply(k);
        apply(mined_state); apply(visible); apply(morton_key);
        for (auto& [_, vec] : attributes) apply(vec);
        for (auto& [_, interned] : string_attributes) apply(interned.indices);
    }

    // Helper for memory diagnostics
    size_t current_memory_usage() const {
        size_t total_bytes = 0;

        // Fixed-size vectors
        auto vec_usage = [](const auto& v) {
            using T = typename std::decay_t<decltype(v)>::value_type;
            return v.capacity() * sizeof(T);
        };

        total_bytes += vec_usage(x) + vec_usage(y) + vec_usage(z);
        total_bytes += vec_usage(x_span) + vec_usage(y_span) + vec_usage(z_span);
        total_bytes += vec_usage(i) + vec_usage(j) + vec_usage(k);
        total_bytes += vec_usage(mined_state) + vec_usage(visible) + vec_usage(morton_key);

        // Numeric attributes
        for (const auto& pair : attributes) {
            total_bytes += vec_usage(pair.second);
        }

        // Categorical attributes (Interned)
        for (const auto& pair : string_attributes) {
            total_bytes += pair.second.memory_usage();
        }

        return total_bytes;
    }
};

} // namespace Mining
