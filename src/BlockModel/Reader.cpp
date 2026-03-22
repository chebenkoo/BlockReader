#include "BlockModel/Reader.h"
#include "fast_float.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace Mining {

static double parse_double(const std::string& s) {
    double val;
    auto res = fast_float::from_chars(s.data(), s.data() + s.size(), val);
    if (res.ec != std::errc()) return 0.0;
    return val;
}

static float parse_float(const std::string& s) {
    float val;
    auto res = fast_float::from_chars(s.data(), s.data() + s.size(), val);
    if (res.ec != std::errc()) return 0.0f;
    return val;
}

static int32_t parse_int(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

BlockModelSoA Reader::load_from_csv(
    const std::string& file_path, 
    const ColumnMapping& mapping,
    ProgressCallback callback
) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + file_path);
    }

    std::string header_line;
    if (!std::getline(file, header_line)) {
        throw std::runtime_error("File is empty: " + file_path);
    }

    // Determine delimiter (common in mining: comma or whitespace)
    char delimiter = (header_line.find(',') != std::string::npos) ? ',' : ' ';
    
    // Parse header to map column indices
    std::vector<std::string> headers = split_line(header_line, delimiter);
    std::map<std::string, int> col_map;
    for (int i = 0; i < (int)headers.size(); ++i) {
        std::string h = headers[i];
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        col_map[h] = i;
    }

    auto get_col = [&](const std::string& name) -> int {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return col_map.count(n) ? col_map[n] : -1;
    };

    int idx_x = get_col(mapping.x_col);
    int idx_y = get_col(mapping.y_col);
    int idx_z = get_col(mapping.z_col);
    int idx_i = get_col(mapping.i_col);
    int idx_j = get_col(mapping.j_col);
    int idx_k = get_col(mapping.k_col);

    // Map the dynamic attributes
    std::map<std::string, int> attr_indices;
    for (auto const& [internal_name, csv_header] : mapping.attribute_map) {
        int idx = get_col(csv_header);
        if (idx >= 0) {
            attr_indices[internal_name] = idx;
        }
    }

    BlockModelSoA model;
    // Pre-initialize vectors for dynamic attributes
    for (auto const& [name, _] : attr_indices) {
        model.add_attribute(name);
    }
    
    std::string line;
    size_t count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> row = split_line(line, delimiter);
        if (row.empty()) continue;

        // X, Y, Z
        double x = (idx_x >= 0 && idx_x < (int)row.size()) ? parse_double(row[idx_x]) : 0.0;
        double y = (idx_y >= 0 && idx_y < (int)row.size()) ? parse_double(row[idx_y]) : 0.0;
        double z = (idx_z >= 0 && idx_z < (int)row.size()) ? parse_double(row[idx_z]) : 0.0;

        model.x.push_back(x - mapping.offset_x);
        model.y.push_back(y - mapping.offset_y);
        model.z.push_back(z - mapping.offset_z);

        // I, J, K
        int32_t i = (idx_i >= 0 && idx_i < (int)row.size()) ? parse_int(row[idx_i]) : 0;
        int32_t j = (idx_j >= 0 && idx_j < (int)row.size()) ? parse_int(row[idx_j]) : 0;
        int32_t k = (idx_k >= 0 && idx_k < (int)row.size()) ? parse_int(row[idx_k]) : 0;
        model.i.push_back(i);
        model.j.push_back(j);
        model.k.push_back(k);

        // Dynamic Attributes
        for (auto const& [name, col_idx] : attr_indices) {
            float val = (col_idx < (int)row.size()) ? parse_float(row[col_idx]) : 0.0f;
            model.attributes[name].push_back(val);
        }
        
        model.mined_state.push_back(0);

        // Generate Morton Key (Task 1.1)
        model.morton_key.push_back(SpatialLocality::encode_morton_3d(
            static_cast<uint32_t>(i), 
            static_cast<uint32_t>(j), 
            static_cast<uint32_t>(k)
        ));

        count++;
        if (callback && count % 10000 == 0) {
            callback({count, 0, "Loading blocks..."});
        }
    }

    return model;
}

std::vector<std::string> Reader::split_line(const std::string& line, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        // Trim whitespace if comma delimited
        if (delimiter == ',') {
            item.erase(0, item.find_first_not_of(" \t\r\n"));
            item.erase(item.find_last_not_of(" \t\r\n") + 1);
        }
        if (!item.empty() || delimiter == ',') {
            result.push_back(item);
        }
    }
    return result;
}

} // namespace Mining
