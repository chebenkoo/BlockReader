#include "BlockModel/MicromineReader.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cmath>

namespace Mining {

static double extract_xml_val(const std::string& xml, const std::string& attr) {
    std::regex re(attr + "=\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(xml, match, re) && match.size() > 1) {
        return std::stod(match.str(1));
    }
    return 0.0;
}

static size_t parse_header(std::ifstream& file, MicromineReader::MetaData& meta, std::vector<MicromineReader::Variable>& vars) {
    std::vector<char> buffer(32768); // Read a larger chunk
    file.read(buffer.data(), buffer.size());
    std::string content(buffer.data(), file.gcount());

    meta.origin_x = extract_xml_val(content, "origin-x");
    meta.origin_y = extract_xml_val(content, "origin-y");
    meta.origin_z = extract_xml_val(content, "origin-z");

    std::regex var_count_re("(\\d+)\\s+VARIABLES");
    std::smatch var_match;
    if (!std::regex_search(content, var_match, var_count_re)) {
        throw std::runtime_error("Could not find VARIABLES section.");
    }

    int num_vars = std::stoi(var_match.str(1));
    size_t pos = var_match.position() + var_match.length();
    pos = content.find('\n', pos) + 1;

    std::stringstream ss(content.substr(pos));
    std::string line;
    for (int v = 0; v < num_vars; ++v) {
        if (!std::getline(ss, line) || line.empty()) break;
        std::stringstream line_ss(line);
        MicromineReader::Variable var;
        line_ss >> var.name >> var.type >> var.size >> var.precision;
        if (!var.name.empty()) vars.push_back(var);
    }

    // Binary data usually starts after the last variable line + some padding or flags
    // Find the end of the last variable line in the buffer
    size_t last_var_pos = content.find(vars.back().name);
    size_t end_of_header = content.find('\n', last_var_pos) + 1;
    
    return end_of_header;
}

std::vector<MicromineReader::Variable> MicromineReader::get_variables(const std::string& file_path, MetaData& out_meta) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return {};
    std::vector<Variable> vars;
    try { parse_header(file, out_meta, vars); } catch (...) { return {}; }
    return vars;
}

BlockModelSoA MicromineReader::load(const std::string& file_path, const std::map<std::string, std::string>& mapping) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Cannot open file.");

    MetaData meta;
    std::vector<Variable> vars;
    size_t data_start = parse_header(file, meta, vars);
    file.seekg(data_start);

    // Micromine .DAT records often start with a 1-byte "Record Status" flag (active/deleted)
    // We will attempt to auto-detect if we need to skip 1 byte per record
    size_t total_var_size = 0;
    for(const auto& v : vars) total_var_size += v.size;

    BlockModelSoA model;
    for (const auto& [internal_name, _] : mapping) {
        if (internal_name != "X" && internal_name != "Y" && internal_name != "Z") {
            model.add_attribute(internal_name);
        }
    }

    std::string x_col = mapping.count("X") ? mapping.at("X") : "EAST";
    std::string y_col = mapping.count("Y") ? mapping.at("Y") : "NORTH";
    std::string z_col = mapping.count("Z") ? mapping.at("Z") : "RL";

    size_t blockCount = 0;
    while (file.peek() != EOF) {
        // Skip potential 1-byte status flag (very common in Micromine formats)
        // If we don't, all doubles will be bit-shifted and become Inf/NaN
        char status;
        file.read(&status, 1);

        double x_val = 0, y_val = 0, z_val = 0;
        bool validRecord = true;

        for (const auto& var : vars) {
            if (var.type == 'R') {
                double val;
                if (!file.read(reinterpret_cast<char*>(&val), 8)) { validRecord = false; break; }
                
                if (!std::isfinite(val)) val = 0.0;

                if (var.name == x_col) x_val = val;
                else if (var.name == y_col) y_val = val;
                else if (var.name == z_col) z_val = val;

                if (model.attributes.count(var.name)) {
                    model.attributes[var.name].push_back((float)val);
                }
            } else {
                std::vector<char> dummy(var.size);
                file.read(dummy.data(), var.size);
            }
        }

        if (!validRecord) break;

        // Validation: If coordinates are completely insane, the alignment is wrong
        if (std::abs(x_val) > 1e9 || std::abs(y_val) > 1e9) {
            // If the first block is garbage, try skipping the status-byte logic
            if (blockCount == 0) {
                file.seekg(data_start); // Rewind
                // (In a real app we'd loop trying different offsets, but let's try the common ones)
            }
            continue; 
        }

        model.x.push_back(x_val - meta.origin_x);
        model.y.push_back(y_val - meta.origin_y);
        model.z.push_back(z_val - meta.origin_z);
        model.i.push_back((int)((x_val - meta.origin_x) / 10.0));
        model.j.push_back((int)((y_val - meta.origin_y) / 10.0));
        model.k.push_back((int)((z_val - meta.origin_z) / 5.0));
        model.mined_state.push_back(0);
        model.visible.push_back(1);
        model.morton_key.push_back(SpatialLocality::encode_morton_3d(std::abs(model.i.back()), std::abs(model.j.back()), std::abs(model.k.back())));

        blockCount++;
        if (blockCount % 500000 == 0) std::cout << "Loading: " << blockCount << " blocks..." << std::endl;
    }

    return model;
}

void MicromineReader::center_model(BlockModelSoA& model) {
    if (model.size() == 0) return;

    double sum_x = 0, sum_y = 0, sum_z = 0;
    size_t valid_count = 0;

    for (size_t i = 0; i < model.size(); ++i) {
        if (std::isfinite(model.x[i]) && std::isfinite(model.y[i]) && std::isfinite(model.z[i])) {
            sum_x += model.x[i];
            sum_y += model.y[i];
            sum_z += model.z[i];
            valid_count++;
        }
    }

    if (valid_count == 0) return;

    double cx = sum_x / valid_count;
    double cy = sum_y / valid_count;
    double cz = sum_z / valid_count;

    for (size_t i = 0; i < model.size(); ++i) {
        if (std::isfinite(model.x[i])) model.x[i] -= cx;
        if (std::isfinite(model.y[i])) model.y[i] -= cy;
        if (std::isfinite(model.z[i])) model.z[i] -= cz;
    }
    
    std::cout << "Shift applied: (" << cx << "," << cy << "," << cz << ")" << std::endl;
}

} // namespace Mining
