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
    std::regex re(attr + "=\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(xml, match, re) && match.size() > 1) {
        try { return std::stod(match.str(1)); } catch (...) {}
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
    std::smatch match;
    if (!std::regex_search(content, match, var_re)) throw std::runtime_error("VARIABLES not found");

    int num_vars = std::stoi(match.str(1));
    size_t pos = content.find('\n', match.position() + match.length()) + 1;
    std::stringstream ss(content.substr(pos));
    std::string line;
    for (int v = 0; v < num_vars; ++v) {
        if (!std::getline(ss, line) || line.length() < 5) break;
        std::stringstream ls(line);
        Variable var; std::string t;
        ls >> var.name >> t >> var.size >> var.precision;
        var.type = t.empty() ? 'R' : t[0];
        vars.push_back(var);
    }
    size_t vars_page_start = (match.position() / PageSize) * PageSize;
    return vars_page_start + PageSize;
}

std::vector<MicromineReader::Variable> MicromineReader::get_variables(const std::string& file_path, MetaData& out_meta) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return {};
    std::vector<Variable> vars;
    try {
        parse_header(file, out_meta, vars);
    } catch (...) {
        return {};
    }
    return vars;
}

double MicromineReader::read_field_double(const char* src, const Variable& var) {
    if (var.type == 'R' || var.type == 'D') { double v; std::memcpy(&v, src, 8); return v; }
    if (var.type == 'S' || var.type == 'F') { float v; std::memcpy(&v, src, 4); return (double)v; }
    return 0.0;
}

BlockModelSoA MicromineReader::load(const std::string& file_path, const std::map<std::string, std::string>& mapping, Framework*) {
    std::ifstream file(file_path, std::ios::binary);
    MetaData meta; std::vector<Variable> vars;
    size_t header_end = parse_header(file, meta, vars);

    size_t stride = 0;
    for (const auto& v : vars) stride += v.size;

    auto get_idx = [&](const std::string& key, const std::string& def) {
        std::string target = mapping.count(key) ? mapping.at(key) : def;
        for(int i=0; i<(int)vars.size(); ++i) if(vars[i].name == target) return i;
        return -1;
    };

    int ix = get_idx("X", "EAST"), iy = get_idx("Y", "NORTH"), iz = get_idx("Z", "RL");
    int ixs = get_idx("XSPAN", "_EAST"), iys = get_idx("YSPAN", "_NORTH"), izs = get_idx("ZSPAN", "_RL");
    int ig = get_idx("Grade", "AuCut");

    std::vector<size_t> offs(vars.size()); size_t cur = 0;
    for(int i=0; i<(int)vars.size(); ++i) { offs[i] = cur; cur += vars[i].size; }

    // --- AUTO-SYNC ---
    size_t data_start = header_end;
    for (size_t s = 0; s < 1024; ++s) {
        file.clear(); file.seekg(header_end + s);
        std::vector<char> probe(stride);
        if (!file.read(probe.data(), stride)) break;
        double xv = read_field_double(probe.data() + offs[ix], vars[ix]);
        if (std::isfinite(xv) && std::abs(xv - meta.origin_x) < 10000.0) {
            data_start = header_end + s;
            break;
        }
    }

    BlockModelSoA model;
    if (ig >= 0) model.add_attribute("Grade");

    file.seekg(data_start);
    std::vector<char> buf(stride);
    while (file.read(buf.data(), stride)) {
        double xv = read_field_double(buf.data() + offs[ix], vars[ix]);
        double yv = read_field_double(buf.data() + offs[iy], vars[iy]);
        double zv = read_field_double(buf.data() + offs[iz], vars[iz]);
        double xsv = (ixs >= 0) ? read_field_double(buf.data() + offs[ixs], vars[ixs]) : 10.0;
        double ysv = (iys >= 0) ? read_field_double(buf.data() + offs[iys], vars[iys]) : 10.0;
        double zsv = (izs >= 0) ? read_field_double(buf.data() + offs[izs], vars[izs]) : 5.0;
        double gv = (ig >= 0) ? read_field_double(buf.data() + offs[ig], vars[ig]) : 0.0;

        // STRICT FINITE CHECK
        if (!std::isfinite(xv) || !std::isfinite(yv) || !std::isfinite(zv)) continue;
        if (std::abs(xv) > 1e10 || std::abs(yv) > 1e10) continue; // Out of planet Earth check

        model.x.push_back(xv - meta.origin_x);
        model.y.push_back(yv - meta.origin_y);
        model.z.push_back(zv - meta.origin_z);
        model.x_span.push_back((float)(std::isfinite(xsv) ? xsv : 5.0));
        model.y_span.push_back((float)(std::isfinite(ysv) ? ysv : 5.0));
        model.z_span.push_back((float)(std::isfinite(zsv) ? zsv : 2.5));
        model.visible.push_back(1);
        model.mined_state.push_back(0);
        if (ig >= 0) model.attributes["Grade"].push_back((float)(std::isfinite(gv) ? gv : 0.0f));
    }

    return model;
}

void MicromineReader::center_model(BlockModelSoA& model) {
    if (model.empty()) return;
    double sx=0, sy=0, sz=0; size_t c=0;
    for(size_t i=0; i<model.size(); ++i) {
        if(std::isfinite(model.x[i]) && std::isfinite(model.y[i]) && std::isfinite(model.z[i])) { 
            sx+=model.x[i]; sy+=model.y[i]; sz+=model.z[i]; c++; 
        }
    }
    if(c==0) return;
    double cx=sx/c, cy=sy/c, cz=sz/c;
    for(size_t i=0; i<model.size(); ++i) { 
        if(std::isfinite(model.x[i])) { model.x[i]-=cx; model.y[i]-=cy; model.z[i]-=cz; }
    }
}

Mat4 MicromineReader::RotationData::build_model_to_world() const {
    return Mat4::translation(-pivot_x, -pivot_y, -pivot_z)
         * Mat4::rotation_y(-rotation_y_deg)
         * Mat4::rotation_x(-rotation_x_deg)
         * Mat4::rotation_z(-rotation_z_deg)
         * Mat4::translation(pivot_x, pivot_y, pivot_z);
}

} // namespace Mining
