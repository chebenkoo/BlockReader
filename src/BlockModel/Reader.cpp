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
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Could not open file: " + file_path);

    // 1. Estimate record count for pre-allocation (SoA optimization)
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string header_line;
    if (!std::getline(file, header_line)) throw std::runtime_error("File is empty.");
    
    char delimiter = (header_line.find(',') != std::string::npos) ? ',' : ' ';
    // Parse header to map column indices
    std::vector<std::string> headers = split_line(header_line, delimiter);
    std::map<std::string, int> col_map;

    auto clean_header = [](std::string s) {
        // Remove BOM
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s.erase(0, 3);
        const char* trim_chars = " \t\r\n\"";
        size_t first = s.find_first_not_of(trim_chars);
        if (first == std::string::npos) return std::string();
        size_t last = s.find_last_not_of(trim_chars);
        return s.substr(first, (last - first + 1));
    };

    for (int i = 0; i < (int)headers.size(); ++i) {
        std::string h = clean_header(headers[i]);
        if (h.empty()) continue;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        col_map[h] = i;
    }

    auto get_col = [&](const std::string& name) -> int {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return col_map.count(n) ? col_map[n] : -1;
    };

    int idx_x = get_col(mapping.x_col), idx_y = get_col(mapping.y_col), idx_z = get_col(mapping.z_col);
    int idx_xs = get_col(mapping.x_span_col), idx_ys = get_col(mapping.y_span_col), idx_zs = get_col(mapping.z_span_col);
    int idx_i = get_col(mapping.i_col), idx_j = get_col(mapping.j_col), idx_k = get_col(mapping.k_col);

    if (idx_xs < 0 || idx_ys < 0 || idx_zs < 0)
        std::cout << "[Reader] WARNING: Span columns not mapped "
                  << "(xs=" << idx_xs << " ys=" << idx_ys << " zs=" << idx_zs
                  << "). All blocks will use default spans 10/10/5.\n"
                  << "         To fix: map X Span / Y Span / Z Span in the field dialog.\n";

    auto clean_token = [](std::string& s) {
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s.erase(0, 3);
        const char* trim_chars = " \t\r\n\"";
        size_t first = s.find_first_not_of(trim_chars);
        if (first == std::string::npos) { s.clear(); return; }
        size_t last = s.find_last_not_of(trim_chars);
        s = s.substr(first, (last - first + 1));
    };

    // --- Type probe: read first 10 data rows to classify each attribute column ---
    // For each column: if majority of sampled values fail float parsing → STRING.
    enum class ColType { NUMERIC, STRING };
    std::map<std::string, ColType> col_types;
    {
        auto probe_pos = file.tellg();
        std::map<std::string, int> pass_count, fail_count;

        std::string probe_line;
        int probed = 0;
        while (probed < 10 && std::getline(file, probe_line)) {
            if (probe_line.empty()) continue;
            std::vector<std::string> probe_row;
            size_t ps = 0, pe = probe_line.find(delimiter);
            while (pe != std::string::npos) {
                std::string t = probe_line.substr(ps, pe - ps); clean_token(t);
                probe_row.push_back(t); ps = pe + 1; pe = probe_line.find(delimiter, ps);
            }
            std::string lt = probe_line.substr(ps); clean_token(lt);
            probe_row.push_back(lt);

            for (auto const& [name, csv_header] : mapping.attribute_map) {
                int col_idx = get_col(csv_header);
                if (col_idx < 0 || col_idx >= (int)probe_row.size()) continue;
                const std::string& val = probe_row[col_idx];
                if (val.empty()) continue;
                float tmp;
                auto res = fast_float::from_chars(val.data(), val.data() + val.size(), tmp);
                if (res.ec == std::errc()) pass_count[name]++;
                else                       fail_count[name]++;
            }
            probed++;
        }
        // Reset to just after header for the main parse loop
        file.seekg(probe_pos);

        for (auto const& [internal_name, csv_header] : mapping.attribute_map) {
            int p = pass_count.count(internal_name) ? pass_count[internal_name] : 0;
            int f = fail_count.count(internal_name) ? fail_count[internal_name] : 0;
            col_types[internal_name] = (f > p) ? ColType::STRING : ColType::NUMERIC;
            std::cout << "[Reader] Column '" << internal_name << "' ("
                      << csv_header << "): "
                      << (col_types[internal_name] == ColType::STRING ? "STRING" : "NUMERIC")
                      << " (pass=" << p << " fail=" << f << ")\n";
        }
    }

    BlockModelSoA model;
    std::map<std::string, int> attr_indices;
    for (auto const& [internal_name, csv_header] : mapping.attribute_map) {
        int idx = get_col(csv_header);
        if (idx >= 0) {
            attr_indices[internal_name] = idx;
            if (col_types[internal_name] == ColType::STRING)
                model.add_string_attribute(internal_name);
            else
                model.add_attribute(internal_name);
        }
    }

    // Heuristic: ~100 bytes per line for mining CSVs
    size_t estimated_rows = file_size / 100;
    model.reserve(estimated_rows);

    std::string line;
    size_t count = 0;
    double cell_x = 10.0, cell_y = 10.0, cell_z = 5.0;
    bool cell_size_initialized = false;

    std::vector<std::string> row;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        row.clear();
        // Inline splitting for performance
        size_t start = 0;
        size_t end = line.find(delimiter);
        while (end != std::string::npos) {
            std::string token = line.substr(start, end - start);
            clean_token(token);
            row.push_back(token);
            start = end + 1;
            end = line.find(delimiter, start);
        }
        std::string lastToken = line.substr(start);
        clean_token(lastToken);
        row.push_back(lastToken);

        if (row.size() < 3) continue;

        // Parse and push directly to arrays (SoA pattern)
        double x = (idx_x >= 0 && idx_x < (int)row.size()) ? parse_double(row[idx_x]) : 0.0;
        double y = (idx_y >= 0 && idx_y < (int)row.size()) ? parse_double(row[idx_y]) : 0.0;
        double z = (idx_z >= 0 && idx_z < (int)row.size()) ? parse_double(row[idx_z]) : 0.0;
        
        float xs = (idx_xs >= 0 && idx_xs < (int)row.size()) ? parse_float(row[idx_xs]) : 10.0f;
        float ys = (idx_ys >= 0 && idx_ys < (int)row.size()) ? parse_float(row[idx_ys]) : 10.0f;
        float zs = (idx_zs >= 0 && idx_zs < (int)row.size()) ? parse_float(row[idx_zs]) : 5.0f;

        if (!cell_size_initialized && xs > 0) { cell_x = xs; cell_y = ys; cell_z = zs; cell_size_initialized = true; }

        int32_t i = (idx_i >= 0 && idx_i < (int)row.size()) ? parse_int(row[idx_i]) : static_cast<int32_t>(std::round(x / cell_x));
        int32_t j = (idx_j >= 0 && idx_j < (int)row.size()) ? parse_int(row[idx_j]) : static_cast<int32_t>(std::round(y / cell_y));
        int32_t k = (idx_k >= 0 && idx_k < (int)row.size()) ? parse_int(row[idx_k]) : static_cast<int32_t>(std::round(z / cell_z));

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

        for (auto const& [name, col_idx] : attr_indices) {
            const std::string& raw = (col_idx < (int)row.size()) ? row[col_idx] : "";
            if (col_types.at(name) == ColType::STRING) {
                model.string_attributes[name].push_back(raw);
            } else {
                model.attributes[name].push_back(raw.empty() ? 0.0f : parse_float(raw));
            }
        }

        count++;
        if (callback && count % 100000 == 0) callback({count, 0, "Loading SoA Blocks..."});
    }

    // --- Load summary ---
    std::cout << "[Reader] Loaded " << count << " blocks from: " << file_path << "\n";
    std::cout << "[Reader] Column mapping: X='" << mapping.x_col << "'(" << idx_x
              << ") Y='" << mapping.y_col << "'(" << idx_y
              << ") Z='" << mapping.z_col << "'(" << idx_z << ")\n";
    std::cout << "[Reader] Span columns: XS='" << mapping.x_span_col << "'(" << idx_xs
              << ") YS='" << mapping.y_span_col << "'(" << idx_ys
              << ") ZS='" << mapping.z_span_col << "'(" << idx_zs << ")\n";
    if (!model.x.empty()) {
        float xMin=*std::min_element(model.x.begin(),model.x.end());
        float xMax=*std::max_element(model.x.begin(),model.x.end());
        float yMin=*std::min_element(model.y.begin(),model.y.end());
        float yMax=*std::max_element(model.y.begin(),model.y.end());
        float zMin=*std::min_element(model.z.begin(),model.z.end());
        float zMax=*std::max_element(model.z.begin(),model.z.end());
        std::cout << "[Reader] X range: [" << xMin << ", " << xMax << "]\n";
        std::cout << "[Reader] Y range: [" << yMin << ", " << yMax << "]\n";
        std::cout << "[Reader] Z range: [" << zMin << ", " << zMax << "]\n";
        if (!model.x_span.empty())
            std::cout << "[Reader] First block spans: xs=" << model.x_span[0]
                      << " ys=" << model.y_span[0] << " zs=" << model.z_span[0] << "\n";
    }
    std::cout << "[Reader] Attributes:";
    for (auto const& [k, v] : model.attributes) std::cout << " '" << k << "'(" << v.size() << ")";
    std::cout << "\n" << std::flush;

    return model;
}

BlockModelAoS Reader::load_to_aos(
    const std::string& file_path, 
    const ColumnMapping& mapping,
    ProgressCallback callback
) {
    std::ifstream file(file_path);
    if (!file.is_open()) throw std::runtime_error("Could not open file: " + file_path);

    std::string header_line;
    if (!std::getline(file, header_line)) throw std::runtime_error("File is empty: " + file_path);

    char delimiter = (header_line.find(',') != std::string::npos) ? ',' : ' ';
    // Parse header to map column indices
    std::vector<std::string> headers = split_line(header_line, delimiter);
    std::map<std::string, int> col_map;

    auto clean_header = [](std::string s) {
        // Remove BOM
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s.erase(0, 3);
        const char* trim_chars = " \t\r\n\"";
        size_t first = s.find_first_not_of(trim_chars);
        if (first == std::string::npos) return std::string();
        size_t last = s.find_last_not_of(trim_chars);
        return s.substr(first, (last - first + 1));
    };

    for (int i = 0; i < (int)headers.size(); ++i) {
        std::string h = clean_header(headers[i]);
        if (h.empty()) continue;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        col_map[h] = i;
    }

    auto get_col = [&](const std::string& name) -> int {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return col_map.count(n) ? col_map[n] : -1;
    };

    int idx_x = get_col(mapping.x_col), idx_y = get_col(mapping.y_col), idx_z = get_col(mapping.z_col);
    int idx_xs = get_col(mapping.x_span_col), idx_ys = get_col(mapping.y_span_col), idx_zs = get_col(mapping.z_span_col);
    int idx_i = get_col(mapping.i_col), idx_j = get_col(mapping.j_col), idx_k = get_col(mapping.k_col);

    std::map<std::string, int> attr_indices;
    BlockModelAoS model;
    for (auto const& [internal_name, csv_header] : mapping.attribute_map) {
        int idx = get_col(csv_header);
        if (idx >= 0) {
            attr_indices[internal_name] = idx;
            model.attributes[internal_name] = std::vector<float>();
        }
    }

    std::string line;
    size_t count = 0;
    double cell_x = 10.0, cell_y = 10.0, cell_z = 5.0;
    bool cell_size_initialized = false;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> row = split_line(line, delimiter);
        if (row.size() < 3) continue;

        double x = (idx_x >= 0 && idx_x < (int)row.size()) ? parse_double(row[idx_x]) : 0.0;
        double y = (idx_y >= 0 && idx_y < (int)row.size()) ? parse_double(row[idx_y]) : 0.0;
        double z = (idx_z >= 0 && idx_z < (int)row.size()) ? parse_double(row[idx_z]) : 0.0;
        float xs = (idx_xs >= 0 && idx_xs < (int)row.size()) ? parse_float(row[idx_xs]) : 10.0f;
        float ys = (idx_ys >= 0 && idx_ys < (int)row.size()) ? parse_float(row[idx_ys]) : 10.0f;
        float zs = (idx_zs >= 0 && idx_zs < (int)row.size()) ? parse_float(row[idx_zs]) : 5.0f;

        if (!cell_size_initialized && xs > 0) { cell_x = xs; cell_y = ys; cell_z = zs; cell_size_initialized = true; }

        int32_t i = (idx_i >= 0 && idx_i < (int)row.size()) ? parse_int(row[idx_i]) : static_cast<int32_t>(std::round(x / cell_x));
        int32_t j = (idx_j >= 0 && idx_j < (int)row.size()) ? parse_int(row[idx_j]) : static_cast<int32_t>(std::round(y / cell_y));
        int32_t k = (idx_k >= 0 && idx_k < (int)row.size()) ? parse_int(row[idx_k]) : static_cast<int32_t>(std::round(z / cell_z));

        Voxel v;
        v.x = (float)(x - mapping.offset_x); v.y = (float)(y - mapping.offset_y); v.z = (float)(z - mapping.offset_z);
        v.xs = xs; v.ys = ys; v.zs = zs;
        v.i = i; v.j = j; v.k = k;
        v.mined_state = 0; v.visible = 1;
        v.morton_key = SpatialLocality::encode_morton_3d(std::abs(i), std::abs(j), std::abs(k));
        
        model.voxels.push_back(v);

        for (auto const& [name, col_idx] : attr_indices) {
            model.attributes[name].push_back((col_idx < (int)row.size()) ? parse_float(row[col_idx]) : 0.0f);
        }

        count++;
        if (callback && count % 50000 == 0) callback({count, 0, "Loading AoS voxels..."});
    }
    return model;
}

std::vector<std::string> Reader::split_line(const std::string& line, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        // Trim whitespace and quotes if comma delimited
        if (delimiter == ',') {
            item.erase(0, item.find_first_not_of(" \t\r\n\""));
            size_t last = item.find_last_not_of(" \t\r\n\"");
            if (last != std::string::npos) {
                item.erase(last + 1);
            } else {
                item.clear();
            }
        }
        if (!item.empty() || delimiter == ',') {
            result.push_back(item);
        }
    }
    return result;
}

} // namespace Mining
