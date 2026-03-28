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
#include <limits>

namespace Mining {

// ===========================================================================
// RotationData
// ===========================================================================

Mat4 MicromineReader::RotationData::build_model_to_world() const
{
    return Mat4::translation(-pivot_x, -pivot_y, -pivot_z)
         * Mat4::rotation_y(-rotation_y_deg)
         * Mat4::rotation_x(-rotation_x_deg)
         * Mat4::rotation_z(-rotation_z_deg)
         * Mat4::translation(pivot_x, pivot_y, pivot_z);
}

// ===========================================================================
// Helper – extract a double attribute from XML-like string
// ===========================================================================

static double extract_xml_val(const std::string& xml, const std::string& attr)
{
    std::string pattern = attr + "\\s*=\\s*[\"']([^\"']*)[\"']";
    std::regex re(pattern);
    std::smatch m;
    if (std::regex_search(xml, m, re) && m.size() > 1)
    {
        try { return std::stod(m.str(1)); } catch (...) {}
    }
    return 0.0;
}

// ===========================================================================
// parse_header
// ===========================================================================

size_t MicromineReader::parse_header(std::ifstream& file,
                                     MetaData&       meta,
                                     std::vector<Variable>& vars)
{
    file.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::string xml_accumulator;
    std::string vars_page_text;
    size_t vars_page_start = 0;
    bool   found_vars = false;

    const std::string vars_marker = "VARIABLES";

    for (size_t page_offset = 0; page_offset < file_size; page_offset += PageSize)
    {
        std::vector<char> page(PageSize, 0);
        file.seekg(static_cast<std::streamoff>(page_offset));
        file.read(page.data(), PageSize);
        const size_t bytes_read = static_cast<size_t>(file.gcount());
        if (bytes_read == 0) break;

        if (bytes_read >= 4 + vars_marker.size() &&
            std::memcmp(page.data() + 4, vars_marker.data(), vars_marker.size()) == 0)
        {
            vars_page_start = page_offset;
            vars_page_text  = std::string(page.data(), bytes_read);
            found_vars = true;
            break;
        }
        xml_accumulator.append(page.data(), bytes_read);
    }

    if (!found_vars)
        throw std::runtime_error("MicromineReader: VARIABLES section not found.");

    meta.origin_x = extract_xml_val(xml_accumulator, "origin-x");
    meta.origin_y = extract_xml_val(xml_accumulator, "origin-y");
    meta.origin_z = extract_xml_val(xml_accumulator, "origin-z");

    meta.rotation.rotation_x_deg = extract_xml_val(xml_accumulator, "rotation-x");
    meta.rotation.rotation_y_deg = extract_xml_val(xml_accumulator, "rotation-y");
    meta.rotation.rotation_z_deg = extract_xml_val(xml_accumulator, "rotation-z");
    meta.rotation.pivot_x        = extract_xml_val(xml_accumulator, "pivot-x");
    meta.rotation.pivot_y        = extract_xml_val(xml_accumulator, "pivot-y");
    meta.rotation.pivot_z        = extract_xml_val(xml_accumulator, "pivot-z");
    meta.rotation.is_rotated     = (std::abs(meta.rotation.rotation_x_deg) > 0.001 ||
                                    std::abs(meta.rotation.rotation_y_deg) > 0.001 ||
                                    std::abs(meta.rotation.rotation_z_deg) > 0.001);

    std::istringstream ss(vars_page_text);
    std::string line;
    std::getline(ss, line);
    int num_vars = 0;
    std::string num_str;
    for(char c : line) if(std::isdigit(c)) num_str += c; else if(!num_str.empty()) break;
    if(!num_str.empty()) num_vars = std::stoi(num_str);

    for (int v = 0; v < num_vars; ++v)
    {
        if (!std::getline(ss, line)) break;
        if (line.empty()) { --v; continue; }

        std::istringstream ls(line);
        Variable var;
        std::string type_str;
        ls >> var.name >> type_str >> var.size >> var.precision;
        if (var.name.empty()) continue;

        var.type = type_str.empty() ? 'R' : static_cast<char>(std::toupper(type_str[0]));
        if (var.size <= 0)
        {
            switch (var.type)
            {
                case 'R': case 'L': var.size = 8; break;
                case 'S': case 'I': var.size = 4; break;
                case 'T':           var.size = 2; break;
                case 'B':           var.size = 1; break;
                default:            var.size = 8; break;
            }
        }
        vars.push_back(var);
    }

    return vars_page_start + PageSize;
}

size_t MicromineReader::record_stride(const std::vector<Variable>& vars)
{
    size_t n = 0;
    for (const auto& v : vars) n += static_cast<size_t>(v.size);
    return n;
}

double MicromineReader::read_field_double(const char* src, const Variable& var)
{
    switch (var.type)
    {
        case 'R': case 'D': { double v; std::memcpy(&v, src, 8); return v; }
        case 'S': case 'F': { float v; std::memcpy(&v, src, 4); return static_cast<double>(v); }
        case 'I': { int32_t v; std::memcpy(&v, src, 4); return static_cast<double>(v); }
        case 'T': { int16_t v; std::memcpy(&v, src, 2); return static_cast<double>(v); }
        case 'L': { int64_t v; std::memcpy(&v, src, 8); return static_cast<double>(v); }
        case 'B': { uint8_t v; std::memcpy(&v, src, 1); return static_cast<double>(v != 0 ? 1 : 0); }
        default:  return 0.0;
    }
}

int MicromineReader::detect_status_byte(std::ifstream& file,
                                        size_t data_start,
                                        const std::vector<Variable>& vars,
                                        const std::string& x_col,
                                        const std::string& y_col,
                                        const MetaData& meta,
                                        size_t& out_sync_start)
{
    const size_t stride = record_stride(vars);
    int x_off = -1, y_off = -1, x_var = -1, y_var = -1;
    size_t offset = 0;
    for (int idx = 0; idx < static_cast<int>(vars.size()); ++idx)
    {
        if (vars[idx].name == x_col) { x_off = static_cast<int>(offset); x_var = idx; }
        if (vars[idx].name == y_col) { y_off = static_cast<int>(offset); y_var = idx; }
        offset += vars[idx].size;
    }

    if (x_off < 0 || y_off < 0) return 0;

    // Scan both start-offset (0..511) AND prefix size (0..4) simultaneously.
    // For each (start, prefix) pair, read 10 consecutive records and require:
    //   - all X/Y are finite and in plausible UTM/local range (100 .. 10M)
    //   - consecutive X and Y within 10 km of each other
    for (int extra_prefix = 0; extra_prefix <= 4; ++extra_prefix) {
        const size_t rec_size = stride + extra_prefix;
        for (size_t start_off = 0; start_off < 512; ++start_off) {
            file.clear();
            file.seekg(static_cast<std::streamoff>(data_start + start_off));
            std::vector<char> rec(rec_size);
            int good = 0;
            double prev_x = 0.0, prev_y = 0.0;
            for (int i = 0; i < 10; ++i) {
                if (!file.read(rec.data(), static_cast<std::streamsize>(rec_size))) break;
                const char* data = rec.data() + extra_prefix;
                double xv = read_field_double(data + x_off, vars[x_var]);
                double yv = read_field_double(data + y_off, vars[y_var]);
                if (!std::isfinite(xv) || !std::isfinite(yv)) break;
                if (std::abs(xv) < 100.0 || std::abs(xv) > 1.0e7) break;
                if (std::abs(yv) < 100.0 || std::abs(yv) > 1.0e7) break;
                if (i > 0 && (std::abs(xv - prev_x) > 10000.0 || std::abs(yv - prev_y) > 10000.0)) break;
                prev_x = xv; prev_y = yv;
                ++good;
            }
            if (good >= 8) {
                std::cout << "MicromineReader: sync start=+" << start_off
                          << " prefix=" << extra_prefix
                          << " stride=" << rec_size << std::endl;
                // Re-seek to the correct data start for the caller
                out_sync_start = data_start + start_off;
                return extra_prefix;
            }
        }
    }
    std::cout << "MicromineReader: sync FAILED, using prefix=0" << std::endl;
    out_sync_start = data_start;
    return 0;
}

void MicromineReader::rebuild_indices(BlockModelSoA& model, const Framework& fw)
{
    const size_t n = model.size();
    model.i.resize(n); model.j.resize(n); model.k.resize(n);
    model.morton_key.resize(n);
    for (size_t idx = 0; idx < n; ++idx)
    {
        const int ci = static_cast<int>(std::floor(model.x[idx] / fw.cell_size_x));
        const int cj = static_cast<int>(std::floor(model.y[idx] / fw.cell_size_y));
        const int ck = static_cast<int>(std::floor(model.z[idx] / fw.cell_size_z));
        model.i[idx] = ci; model.j[idx] = cj; model.k[idx] = ck;
        model.morton_key[idx] = SpatialLocality::encode_morton_3d(
            static_cast<uint32_t>(std::abs(ci)),
            static_cast<uint32_t>(std::abs(cj)),
            static_cast<uint32_t>(std::abs(ck)));
    }
}

std::vector<MicromineReader::Variable>
MicromineReader::get_variables(const std::string& file_path, MetaData& out_meta)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return {};
    std::vector<Variable> vars;
    try { parse_header(file, out_meta, vars); } catch (...) { return {}; }
    return vars;
}

BlockModelSoA MicromineReader::load(const std::string& file_path,
                                    const std::map<std::string, std::string>& mapping,
                                    Framework* out_framework)
{
    std::ifstream file(file_path, std::ios::binary);
    MetaData meta; std::vector<Variable> vars;
    const size_t header_end = parse_header(file, meta, vars);

    auto col = [&](const std::string& k, const std::string& d) { return mapping.count(k) ? mapping.at(k) : d; };
    const std::string x_col = col("X", "EAST"), y_col = col("Y", "NORTH"), z_col = col("Z", "RL");
    const std::string xs_col = col("XSPAN", "_EAST"), ys_col = col("YSPAN", "_NORTH"), zs_col = col("ZSPAN", "_RL");

    struct ColInfo { int v_idx; size_t byte_off; };
    auto find_col = [&](const std::string& name) -> ColInfo {
        size_t off = 0;
        for (int i = 0; i < (int)vars.size(); ++i) {
            if (vars[i].name == name) return {i, off};
            off += vars[i].size;
        }
        return {-1, 0};
    };

    const ColInfo ci_x = find_col(x_col), ci_y = find_col(y_col), ci_z = find_col(z_col);
    const ColInfo ci_xs = find_col(xs_col), ci_ys = find_col(ys_col), ci_zs = find_col(zs_col);

    std::vector<std::pair<std::string, ColInfo>> attr_cols;
    for (const auto& [internal, file_col] : mapping) {
        if (internal == "X" || internal == "Y" || internal == "Z" || internal == "XSPAN" || internal == "YSPAN" || internal == "ZSPAN") continue;
        auto ci = find_col(file_col);
        if (ci.v_idx >= 0) attr_cols.push_back({internal, ci});
    }

    size_t sync_start = header_end;
    const int status_prefix = detect_status_byte(file, header_end, vars, x_col, y_col, meta, sync_start);
    std::cout << "MicromineReader: status_prefix=" << status_prefix << std::endl;

    BlockModelSoA model;
    for (const auto& a : attr_cols) model.add_attribute(a.first);

    const Mat4 m2w = meta.rotation.is_rotated ? meta.rotation.build_model_to_world() : Mat4::identity();
    const size_t stride = record_stride(vars);
    const size_t rec_size = stride + status_prefix;
    
    double max_xs = 1.0, max_ys = 1.0, max_zs = 1.0;
    double l_cx = 0, l_cy = 0, l_cz = 0;

    file.clear();
    file.seekg(static_cast<std::streamoff>(sync_start));
    
    // FLAT sequential read — records are a contiguous binary stream with no page alignment.
    // A page-based loop discards leftover bytes at each 4096-byte boundary, causing cumulative
    // drift of (4096 % rec_size) bytes per page which corrupts all subsequent records.
    std::vector<char> rec_buf(rec_size);
    size_t total_blocks = 0;

    while (file.read(rec_buf.data(), static_cast<std::streamsize>(rec_size))) {
        const char* data_with_status = rec_buf.data();
        const char* data = data_with_status + status_prefix;

        // Skip deleted records if status prefix is present
        if (status_prefix > 0) {
            uint8_t status = static_cast<uint8_t>(data_with_status[0]);
            if (status == 0x00 || status == 0x02) continue;
        }

            double xv = (ci_x.v_idx >= 0) ? read_field_double(data + ci_x.byte_off, vars[ci_x.v_idx]) : 0.0;
            double yv = (ci_y.v_idx >= 0) ? read_field_double(data + ci_y.byte_off, vars[ci_y.v_idx]) : 0.0;
            double zv = (ci_z.v_idx >= 0) ? read_field_double(data + ci_z.byte_off, vars[ci_z.v_idx]) : 0.0;
            double xs = (ci_xs.v_idx >= 0) ? read_field_double(data + ci_xs.byte_off, vars[ci_xs.v_idx]) : 10.0;
            double ys = (ci_ys.v_idx >= 0) ? read_field_double(data + ci_ys.byte_off, vars[ci_ys.v_idx]) : 10.0;
            double zs = (ci_zs.v_idx >= 0) ? read_field_double(data + ci_zs.byte_off, vars[ci_zs.v_idx]) : 5.0;

            if (std::isfinite(xv) && std::isfinite(yv) && std::isfinite(zv) && std::abs(xv) < 1e9) {
                xv -= meta.origin_x; yv -= meta.origin_y; zv -= meta.origin_z;
                if (meta.rotation.is_rotated) m2w.transform_point(xv, yv, zv);

                // Clamp spans: garbage reads produce astronomically large values
                if (!std::isfinite(xs) || xs <= 0.0 || xs > 500.0) xs = 10.0;
                if (!std::isfinite(ys) || ys <= 0.0 || ys > 500.0) ys = 10.0;
                if (!std::isfinite(zs) || zs <= 0.0 || zs > 200.0) zs = 5.0;

                model.x.push_back((float)xv); model.y.push_back((float)yv); model.z.push_back((float)zv);
                model.x_span.push_back((float)xs); model.y_span.push_back((float)ys); model.z_span.push_back((float)zs);
                model.mined_state.push_back(0); model.visible.push_back(1);
                model.i.push_back(0); model.j.push_back(0); model.k.push_back(0); model.morton_key.push_back(0);

                if (xs > max_xs) { max_xs = xs; l_cx = xv; }
                if (ys > max_ys) { max_ys = ys; l_cy = yv; }
                if (zs > max_zs) { max_zs = zs; l_cz = zv; }

                for (const auto& a : attr_cols) {
                    model.attributes[a.first].push_back((float)read_field_double(data + a.second.byte_off, vars[a.second.v_idx]));
                }
                
                if (total_blocks < 3) {
                    std::cout << "BLOCK[" << total_blocks << "] xyz=(" << xv << "," << yv << "," << zv << ") spans=(" << xs << "," << ys << "," << zs << ")" << std::endl;
                }
                total_blocks++;
            }
    }

    if (model.empty()) return model;

    Framework fw;
    fw.cell_size_x = std::max(1.0, max_xs); fw.cell_size_y = std::max(1.0, max_ys); fw.cell_size_z = std::max(1.0, max_zs);
    fw.origin_x = 0; fw.origin_y = 0; fw.origin_z = 0; // Simplified for world space
    rebuild_indices(model, fw);
    if (out_framework) *out_framework = fw;
    
    std::cout << "MicromineReader: SUCCESS. Count=" << model.size() << " CellSize=(" << fw.cell_size_x << "," << fw.cell_size_y << "," << fw.cell_size_z << ")" << std::endl;
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
    std::cout << "MicromineReader: centered by (" << cx << "," << cy << "," << cz << ")" << std::endl;
}

} // namespace Mining
