#include "BlockModel/Reader.h"
#include "BlockModel/ModelDiagnostics.h"
#include "fast_float.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <charconv>
#include <map>
#include <set>
#include <QDebug>

namespace Mining {

// Transparent hash + equality for unordered_map<string, ...> so that
// find(string_view) compiles without constructing a temporary std::string.
// This eliminates 2.7M heap allocs per string column during parsing (C++20).
struct SvHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const std::string&  s)  const noexcept { return std::hash<std::string_view>{}(s); }
};
struct SvEqual {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    bool operator()(const std::string& a, const std::string& b) const noexcept { return a == b; }
    bool operator()(std::string_view a, const std::string& b) const noexcept { return a == b; }
    bool operator()(const std::string& a, std::string_view b) const noexcept { return a == b; }
};
using InternLookup = std::unordered_map<std::string, int32_t, SvHash, SvEqual>;

static double parse_double(std::string_view s) {
    double val;
    auto res = fast_float::from_chars(s.data(), s.data() + s.size(), val);
    if (res.ec != std::errc()) return 0.0;
    return val;
}

static float parse_float(std::string_view s) {
    float val;
    auto res = fast_float::from_chars(s.data(), s.data() + s.size(), val);
    if (res.ec != std::errc()) return 0.0f;
    return val;
}

static int32_t parse_int(std::string_view s) {
    int32_t val = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), val);
    if (res.ec != std::errc()) return 0;
    return val;
}

static std::string_view trim_view(std::string_view s) {
    const char* trim_chars = " \t\r\n\"";
    size_t first = s.find_first_not_of(trim_chars);
    if (first == std::string_view::npos) return {};
    size_t last = s.find_last_not_of(trim_chars);
    return s.substr(first, (last - first + 1));
}

BlockModelSoA Reader::load_from_csv(
    const std::string& file_path, 
    const ColumnMapping& mapping,
    ProgressCallback callback
) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Could not open file: " + file_path);

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string header_line;
    if (!std::getline(file, header_line)) throw std::runtime_error("File is empty.");
    
    char delimiter = (header_line.find(',') != std::string::npos) ? ',' : ' ';
    
    // Parse header
    std::vector<std::string> headers = split_line(header_line, delimiter);
    std::map<std::string, int> col_map;

    for (int i = 0; i < (int)headers.size(); ++i) {
        std::string h = headers[i];
        // Remove BOM and trim
        if (i == 0 && h.size() >= 3 && (unsigned char)h[0] == 0xEF && (unsigned char)h[1] == 0xBB && (unsigned char)h[2] == 0xBF)
            h.erase(0, 3);
        
        std::string_view hv = trim_view(h);
        std::string clean_h(hv);
        std::transform(clean_h.begin(), clean_h.end(), clean_h.begin(), ::tolower);
        col_map[clean_h] = i;
    }

    auto get_col = [&](const std::string& name) -> int {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return col_map.count(n) ? col_map[n] : -1;
    };

    int idx_x = get_col(mapping.x_col), idx_y = get_col(mapping.y_col), idx_z = get_col(mapping.z_col);
    int idx_xs = get_col(mapping.x_span_col), idx_ys = get_col(mapping.y_span_col), idx_zs = get_col(mapping.z_span_col);
    int idx_i = get_col(mapping.i_col), idx_j = get_col(mapping.j_col), idx_k = get_col(mapping.k_col);

    // Attribute Setup
    enum class ColType { NUMERIC, STRING, RAW_STRING };
    std::map<std::string, ColType> col_types;
    std::map<std::string, int> attr_indices;
    
    // Robust type probe (first 100 rows)
    {
        auto probe_pos = file.tellg();
        std::map<std::string, int> numeric_count;
        std::map<std::string, int> non_empty_count;
        std::map<std::string, std::set<std::string>> unique_samples;
        std::string p_line;
        int rows_probed = 0;
        while (rows_probed < 100 && std::getline(file, p_line)) {
            std::vector<std::string> row = split_line(p_line, delimiter);
            for (auto const& [name, csv_header] : mapping.attribute_map) {
                int cidx = get_col(csv_header);
                if (cidx >= 0 && cidx < (int)row.size()) {
                    std::string_view val = trim_view(row[cidx]);
                    if (!val.empty()) {
                        non_empty_count[name]++;
                        float tmp;
                        auto res = fast_float::from_chars(val.data(), val.data() + val.size(), tmp);
                        if (res.ec == std::errc()) numeric_count[name]++;
                        else unique_samples[name].insert(std::string(val));
                    }
                }
            }
            rows_probed++;
        }
        file.seekg(probe_pos);
        for (auto const& [name, _] : mapping.attribute_map) {
            if (non_empty_count[name] > 0) {
                float numeric_ratio = (float)numeric_count[name] / non_empty_count[name];
                if (numeric_ratio > 0.9f) {
                    col_types[name] = ColType::NUMERIC;
                } else {
                    // CARDINALITY GUARD: If >50% of sampled strings are unique, skip interning.
                    // Interning is for repetitive categories (RockType), not IDs or comments.
                    float unique_ratio = (float)unique_samples[name].size() / (non_empty_count[name] - numeric_count[name] + 1);
                    col_types[name] = (unique_ratio > 0.5f) ? ColType::RAW_STRING : ColType::STRING;
                    if (col_types[name] == ColType::RAW_STRING) {
                        qDebug() << "[LOAD] Column" << name.c_str() << "has high cardinality. Skipping interning.";
                    }
                }
            } else {
                col_types[name] = ColType::NUMERIC;
            }
        }
    }

    BlockModelSoA model;
    
    // Map string to ID. Zero redundant string copies at the end.
    std::map<std::string, InternLookup> local_intern_maps;

    // Pre-fetch pointers to the numeric vectors and string indices to avoid map lookups in the hot loop.
    struct NumericTarget { std::vector<float>* vec; int cidx; };
    struct StringTarget  { BlockModelSoA::InternedString* interned; InternLookup* lookup; int cidx; std::string name; };
    struct RawStringTarget { std::vector<std::string>* vec; int cidx; };
    
    std::vector<NumericTarget> numeric_targets;
    std::vector<StringTarget>  string_targets;
    std::vector<RawStringTarget> raw_string_targets;

    for (auto const& [name, csv_header] : mapping.attribute_map) {
        int idx = get_col(csv_header);
        if (idx >= 0) {
            attr_indices[name] = idx;
            if (col_types[name] == ColType::STRING) {
                model.add_string_attribute(name);
                local_intern_maps[name] = {};
                string_targets.push_back({&model.string_attributes[name], &local_intern_maps[name], idx, name});
            } else if (col_types[name] == ColType::RAW_STRING) {
                // We use InternedString but skip the lookup map. unique_values will store raw strings.
                model.add_string_attribute(name);
                raw_string_targets.push_back({&model.string_attributes[name].unique_values, idx});
            } else {
                model.add_attribute(name);
                numeric_targets.push_back({&model.attributes[name], idx});
            }
        }
    }

    // --- HEURISTIC ROW ESTIMATION ---
    // Accurate reservation is CRITICAL to prevent "Working Set Inflation" on Windows.
    // Growing 60+ parallel vectors simultaneously without reservation causes 
    // catastrophic heap fragmentation, leading to the 18GB "unaccounted" memory.
    size_t estimated_rows = file_size / 250; 
    {
        auto pos = file.tellg();
        std::string sample_line;
        size_t total_len = 0;
        int sampled = 0;
        while (sampled < 20 && std::getline(file, sample_line)) {
            if (!sample_line.empty()) { total_len += sample_line.size() + 1; ++sampled; }
        }
        file.seekg(pos);
        if (sampled > 0) {
            size_t avg_row_len = total_len / sampled;
            if (avg_row_len > 10) estimated_rows = (file_size / avg_row_len) + 1000;
        }
    }

    qDebug() << "[LOAD] Estimated rows:" << estimated_rows << "| Reserving memory...";
    model.reserve(estimated_rows);

    // Also reserve for attribute vectors specifically (numeric and string indices)
    for (auto& target : numeric_targets) target.vec->reserve(estimated_rows);
    for (auto& target : string_targets)  target.interned->reserve(estimated_rows);
    // -------------------------------------------------------------------------

    std::string line;
    size_t count = 0;
    double cell_x = 10.0, cell_y = 10.0, cell_z = 5.0;
    bool cell_size_initialized = false;

    // Memory checkpoint: log every 500 MB of process growth
    size_t last_checkpoint_mb = ModelDiagnostics::processMemoryMB();
    constexpr size_t MEM_LOG_INTERVAL_MB = 500;
    qDebug() << "[MEM] Parse start — process:" << last_checkpoint_mb << "MB";

    // Reuse a single vector of string_views to avoid allocations
    std::vector<std::string_view> row_views;
    row_views.reserve(headers.size());

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        row_views.clear();
        std::string_view lv(line);
        size_t start = 0;
        size_t end = lv.find(delimiter);
        while (end != std::string_view::npos) {
            row_views.push_back(trim_view(lv.substr(start, end - start)));
            start = end + 1;
            end = lv.find(delimiter, start);
        }
        row_views.push_back(trim_view(lv.substr(start)));

        if (row_views.size() < 3) continue;

        double x = (idx_x >= 0 && idx_x < (int)row_views.size()) ? parse_double(row_views[idx_x]) : 0.0;
        double y = (idx_y >= 0 && idx_y < (int)row_views.size()) ? parse_double(row_views[idx_y]) : 0.0;
        double z = (idx_z >= 0 && idx_z < (int)row_views.size()) ? parse_double(row_views[idx_z]) : 0.0;
        float xs = (idx_xs >= 0 && idx_xs < (int)row_views.size()) ? parse_float(row_views[idx_xs]) : 10.0f;
        float ys = (idx_ys >= 0 && idx_ys < (int)row_views.size()) ? parse_float(row_views[idx_ys]) : 10.0f;
        float zs = (idx_zs >= 0 && idx_zs < (int)row_views.size()) ? parse_float(row_views[idx_zs]) : 5.0f;

        if (!cell_size_initialized && xs > 0) { cell_x = xs; cell_y = ys; cell_z = zs; cell_size_initialized = true; }

        int32_t i = (idx_i >= 0 && idx_i < (int)row_views.size()) ? parse_int(row_views[idx_i]) : static_cast<int32_t>(std::round(x / cell_x));
        int32_t j = (idx_j >= 0 && idx_j < (int)row_views.size()) ? parse_int(row_views[idx_j]) : static_cast<int32_t>(std::round(y / cell_y));
        int32_t k = (idx_k >= 0 && idx_k < (int)row_views.size()) ? parse_int(row_views[idx_k]) : static_cast<int32_t>(std::round(z / cell_z));

        model.x.push_back((float)(x - mapping.offset_x));
        model.y.push_back((float)(y - mapping.offset_y));
        model.z.push_back((float)(z - mapping.offset_z));
        model.x_span.push_back(xs);
        model.y_span.push_back(ys);
        model.z_span.push_back(zs);
        model.i.push_back(i);
        model.j.push_back(j);
        model.k.push_back(k);
        model.mined_state.push_back(0);
        model.visible.push_back(1);
        model.morton_key.push_back(SpatialLocality::encode_morton_3d(std::abs(i), std::abs(j), std::abs(k)));

        // HOT LOOP: Numeric Attributes (No map lookups)
        for (auto& target : numeric_targets) {
            std::string_view val = (target.cidx < (int)row_views.size()) ? row_views[target.cidx] : "";
            target.vec->push_back(val.empty() ? 0.0f : parse_float(val));
        }

        // HOT LOOP: String Attributes (Interning with transparent lookup)
        for (auto& target : string_targets) {
            std::string_view val = (target.cidx < (int)row_views.size()) ? row_views[target.cidx] : "";
            if (val.empty()) {
                target.interned->indices.push_back(-1);
            } else {
                auto it = target.lookup->find(val);
                if (it == target.lookup->end()) {
                    int32_t new_idx = static_cast<int32_t>(target.lookup->size());
                    target.lookup->emplace(std::string(val), new_idx);
                    target.interned->indices.push_back(new_idx);
                } else {
                    target.interned->indices.push_back(it->second);
                }
            }
        }

        // HOT LOOP: Raw String Attributes (Skip interning for high cardinality)
        for (auto& target : raw_string_targets) {
            std::string_view val = (target.cidx < (int)row_views.size()) ? row_views[target.cidx] : "";
            target.vec->emplace_back(val);
        }

        count++;
        if (callback && count % 20000 == 0) {
            callback({(size_t)file.tellg(), file_size, "Parsing Blocks..."});
        }

        // Memory checkpoint: log every 500K rows AND whenever process RAM
        // grows by MEM_LOG_INTERVAL_MB since the last log.
        if (count % 500000 == 0) {
            size_t proc_mb = ModelDiagnostics::processMemoryMB();

            // --- Spatial vectors ---
            size_t spatial_bytes = 0;
            spatial_bytes += (model.x.capacity() + model.y.capacity() + model.z.capacity()) * sizeof(float);
            spatial_bytes += (model.x_span.capacity() + model.y_span.capacity() + model.z_span.capacity()) * sizeof(float);
            spatial_bytes += (model.i.capacity() + model.j.capacity() + model.k.capacity()) * sizeof(int);
            spatial_bytes += model.mined_state.capacity() + model.visible.capacity();
            spatial_bytes += model.morton_key.capacity() * sizeof(uint64_t);

            // --- Numeric attribute vectors ---
            size_t numeric_bytes = 0;
            for (auto& t : numeric_targets)
                numeric_bytes += t.vec->capacity() * sizeof(float);

            // --- Interned string indices ---
            size_t intern_idx_bytes = 0;
            for (auto& t : string_targets)
                intern_idx_bytes += t.interned->indices.capacity() * sizeof(int32_t);

            // --- Lookup maps (the suspected spike source) ---
            size_t lookup_bytes = 0;
            for (auto& [name, lmap] : local_intern_maps) {
                // Each unordered_map node ≈ key string + int32 + ~48 bytes overhead
                lookup_bytes += lmap.size() * (sizeof(int32_t) + 48 + 24); // 24 = avg key string size
            }

            // --- Raw string vectors ---
            size_t raw_bytes = 0;
            for (auto& t : raw_string_targets)
                raw_bytes += t.vec->size() * sizeof(std::string);

            qDebug() << "[MEM] rows=" << count
                     << "| process=" << proc_mb << "MB"
                     << "| spatial=" << spatial_bytes / (1024*1024) << "MB"
                     << "| numeric=" << numeric_bytes / (1024*1024) << "MB"
                     << "| intern_idx=" << intern_idx_bytes / (1024*1024) << "MB"
                     << "| lookup_maps=" << lookup_bytes / (1024*1024) << "MB"
                     << "| raw_str=" << raw_bytes / (1024*1024) << "MB"
                     << "| accounted=" << (spatial_bytes + numeric_bytes + intern_idx_bytes + lookup_bytes + raw_bytes) / (1024*1024) << "MB"
                     << "| UNACCOUNTED=" << (int)proc_mb - 336 - (int)((spatial_bytes + numeric_bytes + intern_idx_bytes + lookup_bytes + raw_bytes) / (1024*1024)) << "MB";

            if (proc_mb >= last_checkpoint_mb + MEM_LOG_INTERVAL_MB)
                last_checkpoint_mb = proc_mb;
        }
    }

    if (callback) callback({file_size, file_size, "Parsing complete."});

    // Finalize string pools: move from maps to the model's vectors using extract (C++17).
    for (auto& target : string_targets) {
        auto& pool = target.interned->unique_values;
        pool.resize(target.lookup->size());
        auto& map = *target.lookup;
        while (!map.empty()) {
            auto it = map.begin();
            int32_t idx = it->second;
            auto node = map.extract(it);
            pool[idx] = std::move(node.key());
        }
    }

    qDebug() << "[MEM] Parse end (pre-shrink) — rows=" << count
             << "| process=" << ModelDiagnostics::processMemoryMB() << "MB";

    // Explicitly destroy the local maps to free memory BEFORE returning the model
    local_intern_maps.clear();
    qDebug() << "[MEM] After local_intern_maps.clear() — process:"
             << ModelDiagnostics::processMemoryMB() << "MB";

    model.shrink_to_fit();
    qDebug() << "[MEM] After shrink_to_fit() — process=" << ModelDiagnostics::processMemoryMB() << "MB";

#ifdef _WIN32
    // Force Windows to decommit freed heap pages immediately.
    // After vector doublings during parse, the OS working set contains many
    // freed pages that WorkingSetSize still counts. This call trims them now.
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    qDebug() << "[MEM] After WorkingSetTrim — process=" << ModelDiagnostics::processMemoryMB() << "MB";
#endif

    return model;
}

BlockModelAoS Reader::load_to_aos(const std::string& path, const ColumnMapping& map, ProgressCallback cb) { 
    // Stub or specialized implementation if needed
    return {}; 
}

std::vector<std::string> Reader::split_line(const std::string& line, char delimiter) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = line.find(delimiter);
    while (end != std::string::npos) {
        result.push_back(line.substr(start, end - start));
        start = end + 1;
        end = line.find(delimiter, start);
    }
    result.push_back(line.substr(start));
    return result;
}

} // namespace Mining
