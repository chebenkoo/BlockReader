#include "BlockModel/Reader.h"
#include "fast_float.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <charconv>

namespace Mining {

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
    enum class ColType { NUMERIC, STRING };
    std::map<std::string, ColType> col_types;
    std::map<std::string, int> attr_indices;
    
    // Quick probe for types (first 50 rows)
    {
        auto probe_pos = file.tellg();
        std::map<std::string, int> pass_count;
        std::string p_line;
        int count = 0;
        while (count < 50 && std::getline(file, p_line)) {
            std::vector<std::string> row = split_line(p_line, delimiter);
            for (auto const& [name, csv_header] : mapping.attribute_map) {
                int cidx = get_col(csv_header);
                if (cidx >= 0 && cidx < (int)row.size()) {
                    float tmp;
                    auto res = fast_float::from_chars(row[cidx].data(), row[cidx].data() + row[cidx].size(), tmp);
                    if (res.ec == std::errc()) pass_count[name]++;
                }
            }
            count++;
        }
        file.seekg(probe_pos);
        for (auto const& [name, _] : mapping.attribute_map) {
            col_types[name] = (pass_count[name] > count / 2) ? ColType::NUMERIC : ColType::STRING;
        }
    }

    BlockModelSoA model;
    for (auto const& [name, csv_header] : mapping.attribute_map) {
        int idx = get_col(csv_header);
        if (idx >= 0) {
            attr_indices[name] = idx;
            if (col_types[name] == ColType::STRING) model.add_string_attribute(name);
            else model.add_attribute(name);
        }
    }

    size_t estimated_rows = file_size / 100;
    
    // Improved estimate: check first data row length
    {
        auto pos = file.tellg();
        std::string first_data_line;
        if (std::getline(file, first_data_line)) {
            size_t row_len = first_data_line.size() + 1;
            if (row_len > 10) estimated_rows = file_size / row_len;
        }
        file.seekg(pos);
    }
    
    model.reserve(estimated_rows + 100);

    std::string line;
    size_t count = 0;
    double cell_x = 10.0, cell_y = 10.0, cell_z = 5.0;
    bool cell_size_initialized = false;

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

        for (auto const& [name, cidx] : attr_indices) {
            std::string_view val = (cidx < (int)row_views.size()) ? row_views[cidx] : "";
            if (col_types.at(name) == ColType::STRING) {
                model.string_attributes[name].emplace_back(val);
            } else {
                model.attributes[name].push_back(val.empty() ? 0.0f : parse_float(val));
            }
        }

        count++;
        if (callback && count % 20000 == 0) {
            callback({(size_t)file.tellg(), file_size, "Parsing Blocks..."});
        }
    }

    if (callback) callback({file_size, file_size, "Parsing complete."});
    model.shrink_to_fit();
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
