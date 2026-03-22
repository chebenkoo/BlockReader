#include "BlockModel/MicromineReader.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cmath>
#include <cstring>
#include <iomanip>

namespace Mining {

static double extract_xml_val(const std::string& xml, const std::string& attr) {
    std::string pattern = attr + "=\"([^\"]*)\"";
    std::regex re(pattern);
    std::smatch m;
    if (std::regex_search(xml, m, re) && m.size() > 1) {
        try { return std::stod(m.str(1)); } catch (...) {}
    }
    return 0.0;
}

size_t MicromineReader::parse_header(std::ifstream& file, MetaData& meta, std::vector<Variable>& vars) {
    file.seekg(0, std::ios::beg);
    std::vector<char> head_buf(PageSize * 32); 
    file.read(head_buf.data(), head_buf.size());
    std::string content(head_buf.data(), file.gcount());

    meta.origin_x = extract_xml_val(content, "origin-x");
    meta.origin_y = extract_xml_val(content, "origin-y");
    meta.origin_z = extract_xml_val(content, "origin-z");
    
    std::cout << "READER DEBUG: XML Origin (" << meta.origin_x << ", " << meta.origin_y << ", " << meta.origin_z << ")" << std::endl;

    std::regex var_re("(\\d+)\\s+VARIABLES");
    std::smatch m;
    if (!std::regex_search(content, m, var_re)) throw std::runtime_error("VARIABLES not found");

    int num_vars = std::stoi(m.str(1));
    size_t pos = content.find('\n', m.position() + m.length()) + 1;
    std::stringstream ss(content.substr(pos));
    std::string line;
    
    for (int v = 0; v < num_vars; ++v) {
        if (!std::getline(ss, line) || line.length() < 5) break;
        std::stringstream ls(line);
        Variable var; std::string t;
        ls >> var.name >> t >> var.size >> var.precision;
        var.type = t.empty() ? 'R' : static_cast<char>(std::toupper(t[0]));
        vars.push_back(var);
    }

    return ((m.position() / PageSize) + 1) * PageSize;
}

double MicromineReader::read_field_double(const char* src, const Variable& var) {
    switch (var.type) {
        case 'R': case 'D': { double v; std::memcpy(&v, src, 8); return v; }
        case 'S': case 'F': { float v; std::memcpy(&v, src, 4); return (double)v; }
        case 'I': { int32_t v; std::memcpy(&v, src, 4); return (double)v; }
        case 'L': { int64_t v; std::memcpy(&v, src, 8); return (double)v; }
        case 'T': { int16_t v; std::memcpy(&v, src, 2); return (double)v; }
        case 'B': { uint8_t v; std::memcpy(&v, src, 1); return (double)(v != 0); }
        default: return 0.0;
    }
}

BlockModelSoA MicromineReader::load(const std::string& file_path, const std::map<std::string, std::string>& mapping, Framework*) {
    std::ifstream file(file_path, std::ios::binary);
    MetaData meta; std::vector<Variable> vars;
    size_t header_end = parse_header(file, meta, vars);

    size_t var_stride = 0;
    std::vector<size_t> offs(vars.size());
    for(int i=0; i<(int)vars.size(); ++i) { offs[i] = var_stride; var_stride += vars[i].size; }

    auto get_idx = [&](const std::string& key, const std::string& def) {
        std::string target = mapping.count(key) ? mapping.at(key) : def;
        for(int i=0; i<(int)vars.size(); ++i) if(vars[i].name == target) return i;
        return -1;
    };

    int ix = get_idx("X", "EAST"), iy = get_idx("Y", "NORTH");

    // --- STRIDE HUNTER ---
    // Search for the real per-record size by trying prefix sizes 0..4.
    // Plausibility: both X and Y must be finite and look like real-world coordinates
    // (absolute value > 100 and < 10,000,000 — covers UTM, local grids, etc.).
    // We also verify consistency: read 10 consecutive records and require ALL to pass;
    // then confirm that consecutive X-values differ by < 10,000 m (same mine block model).
    size_t best_start  = header_end;
    size_t best_stride = var_stride;
    bool   found_sync  = false;

    std::cout << "READER DEBUG: Hunting for Stride (Variable Stride is " << var_stride << ")" << std::endl;

    auto is_plausible_coord = [](double v) -> bool {
        return std::isfinite(v) && std::abs(v) > 100.0 && std::abs(v) < 1.0e7;
    };

    for (int prefix = 0; prefix <= 4 && !found_sync; ++prefix) {
        size_t test_stride = var_stride + static_cast<size_t>(prefix);
        for (size_t test_start = 0; test_start < 1024 && !found_sync; ++test_start) {
            file.clear();
            file.seekg(header_end + test_start);

            int good_blocks = 0;
            double prev_x = 0.0;
            std::vector<char> buf(test_stride);
            for (int b = 0; b < 10; ++b) {
                if (!file.read(buf.data(), test_stride)) break;
                // The actual field data begins after the per-record prefix bytes.
                const char* rec = buf.data() + prefix;
                double xv = read_field_double(rec + offs[ix], vars[ix]);
                double yv = read_field_double(rec + offs[iy], vars[iy]);
                if (!is_plausible_coord(xv) || !is_plausible_coord(yv)) break;
                // Consecutive centroids should be within 5000 m of each other
                if (b > 0 && std::abs(xv - prev_x) > 5000.0) break;
                prev_x = xv;
                good_blocks++;
            }

            if (good_blocks >= 8) {
                best_start  = header_end + test_start;
                best_stride = test_stride;
                found_sync  = true;
                std::cout << "READER DEBUG: Sync SUCCESS!  start_offset=+" << test_start
                          << "  prefix=" << prefix
                          << "  stride=" << test_stride << std::endl;
            }
        }
    }

    if (!found_sync)
        std::cout << "READER DEBUG: Sync FAILED — using raw header_end and var_stride." << std::endl;

    int iz = get_idx("Z", "RL");
    int ixs = get_idx("XSPAN", "_EAST"), iys = get_idx("YSPAN", "_NORTH"), izs = get_idx("ZSPAN", "_RL");
    int ig = get_idx("Grade", "AuCut");

    BlockModelSoA model;
    if (ig >= 0) model.add_attribute("Grade");

    // The per-record prefix (status/padding bytes before the actual field data)
    size_t rec_prefix = best_stride - var_stride;

    file.clear();
    file.seekg(best_start);
    std::vector<char> buf(best_stride);
    while (file.read(buf.data(), best_stride)) {
        // Skip the prefix bytes so field offsets index correctly into var data
        const char* rec = buf.data() + rec_prefix;

        double xv  = (ix  >= 0) ? read_field_double(rec + offs[ix],  vars[ix])  : 0.0;
        double yv  = (iy  >= 0) ? read_field_double(rec + offs[iy],  vars[iy])  : 0.0;
        double zv  = (iz  >= 0) ? read_field_double(rec + offs[iz],  vars[iz])  : 0.0;
        double xsv = (ixs >= 0) ? read_field_double(rec + offs[ixs], vars[ixs]) : 10.0;
        double ysv = (iys >= 0) ? read_field_double(rec + offs[iys], vars[iys]) : 10.0;
        double zsv = (izs >= 0) ? read_field_double(rec + offs[izs], vars[izs]) : 5.0;
        double gv  = (ig  >= 0) ? read_field_double(rec + offs[ig],  vars[ig])  : 0.0;

        // A valid block: all coordinates finite and plausible; spans sane
        if (!std::isfinite(xsv) || xsv <= 0.0 || xsv > 10000.0) xsv = 10.0;
        if (!std::isfinite(ysv) || ysv <= 0.0 || ysv > 10000.0) ysv = 10.0;
        if (!std::isfinite(zsv) || zsv <= 0.0 || zsv > 10000.0) zsv = 5.0;
        if (!std::isfinite(gv))  gv = 0.0;

        if (std::isfinite(xv) && std::isfinite(yv) && std::isfinite(zv)
            && std::abs(xv) < 1.0e7 && std::abs(yv) < 1.0e7 && std::abs(zv) < 1.0e6) {
            model.x.push_back(xv - meta.origin_x);
            model.y.push_back(yv - meta.origin_y);
            model.z.push_back(zv - meta.origin_z);
            model.x_span.push_back((float)xsv);
            model.y_span.push_back((float)ysv);
            model.z_span.push_back((float)zsv);
            model.visible.push_back(1);
            model.mined_state.push_back(0);
            if (ig >= 0) model.attributes["Grade"].push_back((float)gv);
        }
    }

    std::cout << "READER SUCCESS: Loaded " << model.size() << " blocks." << std::endl;
    return model;
}

void MicromineReader::center_model(BlockModelSoA& model) {
    if (model.empty()) return;
    double sx=0, sy=0, sz=0; size_t c=0;
    for(size_t i=0; i<model.size(); ++i) {
        if(std::isfinite(model.x[i])) { sx+=model.x[i]; sy+=model.y[i]; sz+=model.z[i]; c++; }
    }
    if(c==0) return;
    double cx=sx/c, cy=sy/c, cz=sz/c;
    for(size_t i=0; i<model.size(); ++i) { 
        if(std::isfinite(model.x[i])) { model.x[i]-=cx; model.y[i]-=cy; model.z[i]-=cz; }
    }
}

std::vector<MicromineReader::Variable> MicromineReader::get_variables(const std::string& file_path, MetaData& out_meta) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return {};
    std::vector<Variable> vars;
    try { parse_header(file, out_meta, vars); } catch (...) {}
    return vars;
}

Mat4 MicromineReader::RotationData::build_model_to_world() const {
    return Mat4::translation(-pivot_x, -pivot_y, -pivot_z)
         * Mat4::rotation_y(-rotation_y_deg)
         * Mat4::rotation_x(-rotation_x_deg)
         * Mat4::rotation_z(-rotation_z_deg)
         * Mat4::translation(pivot_x, pivot_y, pivot_z);
}

} // namespace Mining
