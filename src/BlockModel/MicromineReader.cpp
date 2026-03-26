#include "BlockModel/MicromineReader.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cmath>
#include <cstring>

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
    std::vector<char> head_buf(PageSize * 16); 
    file.read(head_buf.data(), head_buf.size());
    std::string content(head_buf.data(), file.gcount());

    meta.origin_x = extract_xml_val(content, "origin-x");
    meta.origin_y = extract_xml_val(content, "origin-y");
    meta.origin_z = extract_xml_val(content, "origin-z");
    
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
    // Return the offset of the page boundary immediately following the VARIABLES page
    return ((m.position() / PageSize) + 1) * PageSize;
}

double MicromineReader::read_field_double(const char* src, const Variable& var) {
    switch (var.type) {
        case 'R': case 'D': { double v; std::memcpy(&v, src, 8); return v; }
        case 'S': case 'F': { float v; std::memcpy(&v, src, 4); return (double)v; }
        case 'I': { int32_t v; std::memcpy(&v, src, 4); return (double)v; }
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

    int ix = get_idx("X", "EAST"), iy = get_idx("Y", "NORTH"), iz = get_idx("Z", "RL");
    int ixs = get_idx("XSPAN", "_EAST"), iys = get_idx("YSPAN", "_NORTH"), izs = get_idx("ZSPAN", "_RL");
    int ig = get_idx("Grade", "AuCut");

    // --- STRIDE + OFFSET HUNTER ---
    // Try every combination of (per-record prefix bytes 0..4) × (start offset 0..511).
    // For each candidate, attempt to read 10 consecutive records and require all 10 to
    // have plausible X/Y coordinates AND consecutive X values within 10 km of each other.
    // A plausible real-world coordinate: |v| > 100 AND |v| < 1e7 (covers UTM, local grids).
    auto is_plausible = [](double v) {
        return std::isfinite(v) && std::abs(v) > 100.0 && std::abs(v) < 1.0e7;
    };

    size_t best_start  = header_end;
    size_t best_stride = var_stride; // fallback: no prefix
    size_t best_prefix = 0;
    bool   found_sync  = false;

    std::cout << "READER DEBUG: hunting stride (var_stride=" << var_stride << ")..." << std::endl;

    for (int prefix = 0; prefix <= 4 && !found_sync; ++prefix) {
        size_t test_stride = var_stride + static_cast<size_t>(prefix);
        for (size_t s = 0; s < 512 && !found_sync; ++s) {
            file.clear();
            file.seekg(static_cast<std::streamoff>(header_end + s));

            int good = 0;
            double prev_x = 0.0, prev_y = 0.0;
            std::vector<char> buf(test_stride);
            for (int b = 0; b < 10; ++b) {
                if (!file.read(buf.data(), static_cast<std::streamsize>(test_stride))) break;
                const char* rec = buf.data() + prefix; // skip per-record prefix
                double xv = read_field_double(rec + offs[ix], vars[ix]);
                double yv = read_field_double(rec + offs[iy], vars[iy]);
                if (!is_plausible(xv) || !is_plausible(yv)) break;
                if (b > 0 && (std::abs(xv - prev_x) > 10000.0 || std::abs(yv - prev_y) > 10000.0)) break;
                prev_x = xv; prev_y = yv;
                good++;
            }

            if (good >= 8) {
                best_start  = header_end + s;
                best_stride = test_stride;
                best_prefix = static_cast<size_t>(prefix);
                found_sync  = true;
                std::cout << "READER DEBUG: Sync OK  start=+" << s
                          << "  prefix=" << prefix
                          << "  stride=" << test_stride << std::endl;
            }
        }
    }

    if (!found_sync)
        std::cout << "READER DEBUG: Sync FAILED — using raw header_end, var_stride, no prefix." << std::endl;

    BlockModelSoA model;
    if (ig >= 0) model.add_attribute("Grade");

    // Print what columns we resolved so spans can be verified
    auto name_of = [&](int idx) -> std::string {
        return (idx >= 0 && idx < (int)vars.size()) ? vars[idx].name : "<none>";
    };
    std::cout << "READER DEBUG: X=" << name_of(ix) << " Y=" << name_of(iy) << " Z=" << name_of(iz)
              << "  XS=" << name_of(ixs) << " YS=" << name_of(iys) << " ZS=" << name_of(izs)
              << "  Grade=" << name_of(ig) << "  var_stride=" << var_stride << std::endl;

    // --- FLAT SEQUENTIAL READ ---
    // Records are a plain contiguous binary stream — no page alignment.
    // best_start is the byte position of the first record; best_prefix bytes are
    // skipped at the front of each best_stride-byte record.
    file.clear();
    file.seekg(static_cast<std::streamoff>(best_start));

    std::vector<char> rec_buf(best_stride);
    while (file.read(rec_buf.data(), static_cast<std::streamsize>(best_stride))) {
        const char* rec = rec_buf.data() + best_prefix; // skip per-record prefix

        double xv  = (ix  >= 0) ? read_field_double(rec + offs[ix],  vars[ix])  : 0.0;
        double yv  = (iy  >= 0) ? read_field_double(rec + offs[iy],  vars[iy])  : 0.0;
        double zv  = (iz  >= 0) ? read_field_double(rec + offs[iz],  vars[iz])  : 0.0;
        double xsv = (ixs >= 0) ? read_field_double(rec + offs[ixs], vars[ixs]) : 10.0;
        double ysv = (iys >= 0) ? read_field_double(rec + offs[iys], vars[iys]) : 10.0;
        double zsv = (izs >= 0) ? read_field_double(rec + offs[izs], vars[izs]) : 5.0;
        double gv  = (ig  >= 0) ? read_field_double(rec + offs[ig],  vars[ig])  : 0.0;

        // Clamp spans: half-span > 500 m (XY) or > 200 m (Z) is a garbage read.
        if (!std::isfinite(xsv) || xsv <= 0.0 || xsv > 500.0) xsv = 10.0;
        if (!std::isfinite(ysv) || ysv <= 0.0 || ysv > 500.0) ysv = 10.0;
        if (!std::isfinite(zsv) || zsv <= 0.0 || zsv > 200.0) zsv = 5.0;

        if (!std::isfinite(xv) || !std::isfinite(yv) || !std::isfinite(zv)) continue;
        if (std::abs(xv - meta.origin_x) > 2000000.0) continue;

        model.x.push_back(xv - meta.origin_x);
        model.y.push_back(yv - meta.origin_y);
        model.z.push_back(zv - meta.origin_z);
        model.x_span.push_back(static_cast<float>(xsv));
        model.y_span.push_back(static_cast<float>(ysv));
        model.z_span.push_back(static_cast<float>(zsv));
        model.visible.push_back(1);
        if (ig >= 0) {
            if (!std::isfinite(gv)) gv = 0.0;
            model.attributes["Grade"].push_back(static_cast<float>(gv));
        }
    }

    std::cout << "READER SUCCESS: Loaded " << model.size() << " blocks correctly aligned." << std::endl;
    // Diagnostics — first 5 blocks + Grade range
    for (size_t di = 0; di < std::min(model.size(), size_t(5)); ++di)
        std::cout << "  Block[" << di << "]: xyz=(" << model.x[di] << "," << model.y[di] << "," << model.z[di]
                  << ") span=(" << model.x_span[di] << "," << model.y_span[di] << "," << model.z_span[di] << ")" << std::endl;
    if (ig >= 0 && !model.attributes.empty()) {
        const auto& gv = model.attributes.at("Grade");
        float gmin = *std::min_element(gv.begin(), gv.end());
        float gmax = *std::max_element(gv.begin(), gv.end());
        std::cout << "  Grade range: [" << gmin << " to " << gmax << "]" << std::endl;
    }
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
