#include "BlockModel/MicromineReader.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <limits>

namespace Mining {

// ===========================================================================
// RotationData
// ===========================================================================

Mat4 MicromineReader::RotationData::build_model_to_world() const
{
    // Mirrors C# BlockModelFramework constructor:
    //   ModelToWorldMatrix =
    //       Translate(-PoR) × Rotate(Y,-rotY) × Rotate(X,-rotX) × Rotate(Z,-rotZ) × Translate(+PoR)
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
    std::regex re(attr + R"(=\"([^\"]*)\")");
    std::smatch match;
    if (std::regex_search(xml, match, re) && match.size() > 1)
    {
        try { return std::stod(match.str(1)); } catch (...) {}
    }
    return 0.0;
}

// ===========================================================================
// parse_header
//
// Reads the file page-by-page (4096 bytes = 1 Micromine page).
// The Micromine page that contains the VARIABLES section has
// the ASCII string "VARIABLES" starting at byte offset 4 within the page.
// Binary record data begins on the next page boundary after that page.
//
// Also parses XML rotation/origin metadata from the pre-VARIABLES pages.
// ===========================================================================

size_t MicromineReader::parse_header(std::ifstream& file,
                                     MetaData&       meta,
                                     std::vector<Variable>& vars)
{
    file.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::string xml_accumulator;    // collects all pre-VARIABLES pages
    std::string vars_page_text;
    size_t vars_page_start = 0;     // file offset of the VARIABLES page
    bool   found_vars = false;

    const std::string vars_marker = "VARIABLES";

    for (size_t page_offset = 0; page_offset < file_size; page_offset += PageSize)
    {
        std::vector<char> page(PageSize, 0);
        file.seekg(static_cast<std::streamoff>(page_offset));
        file.read(page.data(), PageSize);
        const size_t bytes_read = static_cast<size_t>(file.gcount());
        if (bytes_read == 0) break;

        // Check if this page starts with VARIABLES at byte 4
        // (matches Micromine .dat internal format)
        if (bytes_read >= 4 + vars_marker.size() &&
            std::memcmp(page.data() + 4, vars_marker.data(), vars_marker.size()) == 0)
        {
            vars_page_start = page_offset;
            vars_page_text  = std::string(page.data(), bytes_read);
            found_vars = true;
            break;
        }

        // Accumulate XML metadata pages
        xml_accumulator.append(page.data(), bytes_read);
    }

    if (!found_vars)
        throw std::runtime_error("MicromineReader: VARIABLES section not found in file.");

    // -------------------------------------------------------------------
    // Parse origin and rotation from the XML metadata accumulated so far
    // -------------------------------------------------------------------
    meta.origin_x = extract_xml_val(xml_accumulator, "origin-x");
    meta.origin_y = extract_xml_val(xml_accumulator, "origin-y");
    meta.origin_z = extract_xml_val(xml_accumulator, "origin-z");

    meta.rotation.rotation_x_deg = extract_xml_val(xml_accumulator, "rotation-x");
    meta.rotation.rotation_y_deg = extract_xml_val(xml_accumulator, "rotation-y");
    meta.rotation.rotation_z_deg = extract_xml_val(xml_accumulator, "rotation-z");
    meta.rotation.pivot_x        = extract_xml_val(xml_accumulator, "pivot-x");
    meta.rotation.pivot_y        = extract_xml_val(xml_accumulator, "pivot-y");
    meta.rotation.pivot_z        = extract_xml_val(xml_accumulator, "pivot-z");
    meta.rotation.is_rotated     = (meta.rotation.rotation_x_deg != 0.0 ||
                                    meta.rotation.rotation_y_deg != 0.0 ||
                                    meta.rotation.rotation_z_deg != 0.0);

    // -------------------------------------------------------------------
    // Parse variable descriptors from the VARIABLES page
    // Format (after the "N VARIABLES\r\n" header line):
    //   NAME  TYPE  SIZE  PRECISION
    // -------------------------------------------------------------------
    std::istringstream ss(vars_page_text);
    std::string line;

    // First line: "N VARIABLES" (possibly with \r)
    std::getline(ss, line);
    // Strip CR
    if (!line.empty() && line.back() == '\r') line.pop_back();

    int num_vars = 0;
    {
        std::istringstream ls(line);
        if (!(ls >> num_vars))
            throw std::runtime_error("MicromineReader: Cannot parse variable count.");
    }

    for (int v = 0; v < num_vars; ++v)
    {
        if (!std::getline(ss, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { --v; continue; }   // skip blank lines

        std::istringstream ls(line);
        Variable var;
        std::string type_str;
        ls >> var.name >> type_str >> var.size >> var.precision;
        if (var.name.empty()) continue;

        // Normalise type to single uppercase character
        var.type = type_str.empty() ? 'R' : static_cast<char>(std::toupper(type_str[0]));

        // Ensure size is consistent with type when not specified
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

    if (vars.empty())
        throw std::runtime_error("MicromineReader: No variables parsed from header.");

    // Binary data starts on the page immediately following the VARIABLES page
    const size_t data_start = vars_page_start + PageSize;
    return data_start;
}

// ===========================================================================
// record_stride
// ===========================================================================

size_t MicromineReader::record_stride(const std::vector<Variable>& vars)
{
    size_t n = 0;
    for (const auto& v : vars) n += static_cast<size_t>(v.size);
    return n;
}

// ===========================================================================
// read_field_double
//
// Reads one typed field from raw bytes and returns it as double.
// Mirrors C# GetGeometricFunc / GetUserFunc dispatch.
// ===========================================================================

double MicromineReader::read_field_double(const char* src, const Variable& var)
{
    switch (var.type)
    {
        case 'R': case 'D':             // Real / Double (8 bytes)
        {
            double v; std::memcpy(&v, src, 8);
            return v;
        }
        case 'S': case 'F':             // Single / Float (4 bytes)
        {
            float v; std::memcpy(&v, src, 4);
            return static_cast<double>(v);
        }
        case 'I':                       // Integer (4 bytes)
        {
            int32_t v; std::memcpy(&v, src, 4);
            return static_cast<double>(v);
        }
        case 'T':                       // Short (2 bytes)
        {
            int16_t v; std::memcpy(&v, src, 2);
            return static_cast<double>(v);
        }
        case 'L':                       // Long (8 bytes)
        {
            int64_t v; std::memcpy(&v, src, 8);
            return static_cast<double>(v);
        }
        case 'B':                       // Boolean (1 byte)
        {
            uint8_t v; std::memcpy(&v, src, 1);
            return static_cast<double>(v != 0 ? 1 : 0);
        }
        default:                        // Alpha/string – not numeric, return 0
            return 0.0;
    }
}

// ===========================================================================
// detect_status_byte
//
// Micromine .dat records MAY have a 1-byte active/deleted flag prepended.
// We detect this by reading the first few records with and without the extra
// byte and selecting the alignment that yields finite, plausible coordinates.
// ===========================================================================

int MicromineReader::detect_status_byte(std::ifstream& file,
                                        size_t data_start,
                                        const std::vector<Variable>& vars,
                                        const std::string& x_col,
                                        const std::string& y_col,
                                        const MetaData& meta)
{
    const size_t stride = record_stride(vars);

    // Find the index of x and y columns for probing
    int x_off = -1, y_off = -1;
    int x_var = -1, y_var = -1;
    {
        size_t offset = 0;
        for (int idx = 0; idx < static_cast<int>(vars.size()); ++idx)
        {
            if (vars[idx].name == x_col) { x_off = static_cast<int>(offset); x_var = idx; }
            if (vars[idx].name == y_col) { y_off = static_cast<int>(offset); y_var = idx; }
            offset += vars[idx].size;
        }
    }

    if (x_off < 0 || y_off < 0)
        return 0;   // cannot determine - assume no status byte

    auto probe = [&](int extra_prefix) -> bool
    {
        file.seekg(static_cast<std::streamoff>(data_start));
        const size_t rec_size = stride + extra_prefix;
        std::vector<char> rec(rec_size);

        int good = 0;
        for (int attempt = 0; attempt < 10 && file.read(rec.data(), rec_size); ++attempt)
        {
            const char* data = rec.data() + extra_prefix;
            double xv = read_field_double(data + x_off, vars[x_var]);
            double yv = read_field_double(data + y_off, vars[y_var]);

            if (std::isfinite(xv) && std::isfinite(yv) &&
                std::abs(xv) < 1e8 && std::abs(yv) < 1e8)
                ++good;
        }
        return good >= 5;   // majority of probed records look valid
    };

    if (probe(0)) return 0;   // no status byte
    if (probe(1)) return 1;   // 1-byte status prefix confirmed
    return 0;                  // fall back to no prefix
}

// ===========================================================================
// rebuild_indices
//
// After the full read pass we know the actual CellSize (largest block span).
// Recompute i/j/k grid indices and Morton keys using the correct formula:
//   i = floor((x - origin_x) / cell_size_x)
// ===========================================================================

void MicromineReader::rebuild_indices(BlockModelSoA& model, const Framework& fw)
{
    const size_t n = model.size();
    model.i.resize(n);
    model.j.resize(n);
    model.k.resize(n);
    model.morton_key.resize(n);

    for (size_t idx = 0; idx < n; ++idx)
    {
        // model.x/y/z are already in world space (origin subtracted + rotation applied).
        // Re-derive grid indices from the pre-rotation model-space offset stored
        // plus the framework origin. For simplicity we use the world-space coordinates
        // which, when the model is centred, still give stable relative indices.
        const int ci = static_cast<int>(std::floor(model.x[idx] / fw.cell_size_x));
        const int cj = static_cast<int>(std::floor(model.y[idx] / fw.cell_size_y));
        const int ck = static_cast<int>(std::floor(model.z[idx] / fw.cell_size_z));

        model.i[idx] = ci;
        model.j[idx] = cj;
        model.k[idx] = ck;
        model.morton_key[idx] = SpatialLocality::encode_morton_3d(
            static_cast<uint32_t>(std::abs(ci)),
            static_cast<uint32_t>(std::abs(cj)),
            static_cast<uint32_t>(std::abs(ck)));
    }
}

// ===========================================================================
// get_variables
// ===========================================================================

std::vector<MicromineReader::Variable>
MicromineReader::get_variables(const std::string& file_path, MetaData& out_meta)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return {};
    std::vector<Variable> vars;
    try { parse_header(file, out_meta, vars); } catch (...) { return {}; }
    return vars;
}

// ===========================================================================
// load
// ===========================================================================

BlockModelSoA MicromineReader::load(const std::string& file_path,
                                    const std::map<std::string, std::string>& mapping,
                                    Framework* out_framework)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("MicromineReader: Cannot open file: " + file_path);

    MetaData meta;
    std::vector<Variable> vars;
    const size_t data_start = parse_header(file, meta, vars);

    // -----------------------------------------------------------------------
    // Resolve mapped column names
    // -----------------------------------------------------------------------
    auto col = [&](const std::string& key, const std::string& def) -> std::string
    {
        return mapping.count(key) ? mapping.at(key) : def;
    };

    const std::string x_col     = col("X",     "EAST");
    const std::string y_col     = col("Y",     "NORTH");
    const std::string z_col     = col("Z",     "RL");
    const std::string xspan_col = col("XSPAN", "XINC");
    const std::string yspan_col = col("YSPAN", "YINC");
    const std::string zspan_col = col("ZSPAN", "ZINC");

    // -----------------------------------------------------------------------
    // Pre-map variable index → column byte offset for fast per-record reads
    // -----------------------------------------------------------------------
    struct ColInfo { int  var_idx; size_t byte_offset; };
    auto find_col = [&](const std::string& name) -> ColInfo
    {
        size_t off = 0;
        for (int idx = 0; idx < static_cast<int>(vars.size()); ++idx)
        {
            if (vars[idx].name == name) return {idx, off};
            off += vars[idx].size;
        }
        return {-1, 0};
    };

    const ColInfo ci_x     = find_col(x_col);
    const ColInfo ci_y     = find_col(y_col);
    const ColInfo ci_z     = find_col(z_col);
    const ColInfo ci_xspan = find_col(xspan_col);
    const ColInfo ci_yspan = find_col(yspan_col);
    const ColInfo ci_zspan = find_col(zspan_col);

    // Extra user attributes
    struct AttrInfo { std::string internal_name; ColInfo ci; };
    std::vector<AttrInfo> attr_cols;
    for (const auto& [internal_name, file_col] : mapping)
    {
        if (internal_name == "X"     || internal_name == "Y"     || internal_name == "Z" ||
            internal_name == "XSPAN" || internal_name == "YSPAN" || internal_name == "ZSPAN")
            continue;
        auto ci = find_col(file_col);
        if (ci.var_idx >= 0)
            attr_cols.push_back({internal_name, ci});
    }

    // -----------------------------------------------------------------------
    // Auto-detect status-byte prefix
    // -----------------------------------------------------------------------
    const int status_prefix = detect_status_byte(file, data_start, vars, x_col, y_col, meta);
    if (status_prefix)
        std::cout << "MicromineReader: 1-byte record status prefix detected." << std::endl;

    // -----------------------------------------------------------------------
    // Set up output model
    // -----------------------------------------------------------------------
    BlockModelSoA model;
    for (const auto& a : attr_cols)
        model.add_attribute(a.internal_name);

    // -----------------------------------------------------------------------
    // Build rotation matrix (model space → world space)
    // -----------------------------------------------------------------------
    const bool   apply_rotation = meta.rotation.is_rotated;
    const Mat4   model_to_world = apply_rotation
                                  ? meta.rotation.build_model_to_world()
                                  : Mat4::identity();

    // -----------------------------------------------------------------------
    // Main read loop
    // -----------------------------------------------------------------------
    const size_t stride    = record_stride(vars);
    const size_t rec_size  = stride + status_prefix;
    std::vector<char> rec_buf(rec_size);

    // Track largest block spans to derive CellSize
    double max_xspan = std::numeric_limits<double>::lowest();
    double max_yspan = std::numeric_limits<double>::lowest();
    double max_zspan = std::numeric_limits<double>::lowest();
    // Largest-block centroid (for framework origin calculation)
    double largest_cx = 0, largest_cy = 0, largest_cz = 0;

    file.seekg(static_cast<std::streamoff>(data_start));
    size_t block_count = 0;

    while (file.read(rec_buf.data(), static_cast<std::streamsize>(rec_size)))
    {
        const char* data = rec_buf.data() + status_prefix;

        // Skip deleted records (status byte == 0x00 or 0x02 in Micromine).
        // Only check when status prefix is confirmed.
        if (status_prefix)
        {
            const uint8_t status = static_cast<uint8_t>(rec_buf[0]);
            if (status == 0x00 || status == 0x02)   // deleted / inactive
                continue;
        }

        // ------ Geometry ------
        double xv = (ci_x.var_idx >= 0)
                    ? read_field_double(data + ci_x.byte_offset,     vars[ci_x.var_idx])     : 0.0;
        double yv = (ci_y.var_idx >= 0)
                    ? read_field_double(data + ci_y.byte_offset,     vars[ci_y.var_idx])     : 0.0;
        double zv = (ci_z.var_idx >= 0)
                    ? read_field_double(data + ci_z.byte_offset,     vars[ci_z.var_idx])     : 0.0;

        double xs = (ci_xspan.var_idx >= 0)
                    ? read_field_double(data + ci_xspan.byte_offset, vars[ci_xspan.var_idx]) : 0.0;
        double ys = (ci_yspan.var_idx >= 0)
                    ? read_field_double(data + ci_yspan.byte_offset, vars[ci_yspan.var_idx]) : 0.0;
        double zs = (ci_zspan.var_idx >= 0)
                    ? read_field_double(data + ci_zspan.byte_offset, vars[ci_zspan.var_idx]) : 0.0;

        // Discard NaN/Inf or wildly out-of-range records
        if (!std::isfinite(xv) || !std::isfinite(yv) || !std::isfinite(zv))
            continue;
        if (std::abs(xv) > 1e9 || std::abs(yv) > 1e9 || std::abs(zv) > 1e9)
            continue;

        // ------ Subtract model origin ------
        xv -= meta.origin_x;
        yv -= meta.origin_y;
        zv -= meta.origin_z;

        // ------ Apply rotation (model space → world space) ------
        if (apply_rotation)
            model_to_world.transform_point(xv, yv, zv);

        // ------ Store geometry ------
        model.x.push_back(xv);
        model.y.push_back(yv);
        model.z.push_back(zv);
        model.x_span.push_back(static_cast<float>(xs));
        model.y_span.push_back(static_cast<float>(ys));
        model.z_span.push_back(static_cast<float>(zs));
        model.mined_state.push_back(0);
        model.visible.push_back(1);

        // Placeholders – indices rebuilt after loop once CellSize is known
        model.i.push_back(0);
        model.j.push_back(0);
        model.k.push_back(0);
        model.morton_key.push_back(0);

        // Track largest block (Mirrors C# CalculateFramework logic)
        if (xs > max_xspan) { max_xspan = xs; largest_cx = xv; }
        if (ys > max_yspan) { max_yspan = ys; largest_cy = yv; }
        if (zs > max_zspan) { max_zspan = zs; largest_cz = zv; }

        // ------ User attributes ------
        for (const auto& a : attr_cols)
        {
            const double av = read_field_double(data + a.ci.byte_offset, vars[a.ci.var_idx]);
            model.attributes[a.internal_name].push_back(static_cast<float>(av));
        }

        ++block_count;
        if (block_count % 500000 == 0)
            std::cout << "MicromineReader: loaded " << block_count << " blocks...\n";
    }

    std::cout << "MicromineReader: total " << block_count << " blocks read.\n";

    if (block_count == 0)
        return model;

    // -----------------------------------------------------------------------
    // Derive BlockModelFramework (mirrors C# CalculateFramework)
    // -----------------------------------------------------------------------
    // Fallback for missing span columns
    if (max_xspan <= 0) max_xspan = 1.0;
    if (max_yspan <= 0) max_yspan = 1.0;
    if (max_zspan <= 0) max_zspan = 1.0;

    // Min/max world extents
    double minX = model.x[0], maxX = model.x[0];
    double minY = model.y[0], maxY = model.y[0];
    double minZ = model.z[0], maxZ = model.z[0];
    for (size_t idx = 1; idx < block_count; ++idx)
    {
        minX = std::min(minX, model.x[idx] - model.x_span[idx] * 0.5);
        minY = std::min(minY, model.y[idx] - model.y_span[idx] * 0.5);
        minZ = std::min(minZ, model.z[idx] - model.z_span[idx] * 0.5);
        maxX = std::max(maxX, model.x[idx] + model.x_span[idx] * 0.5);
        maxY = std::max(maxY, model.y[idx] + model.y_span[idx] * 0.5);
        maxZ = std::max(maxZ, model.z[idx] + model.z_span[idx] * 0.5);
    }

    // Grid origin (corner of the cell containing the largest block)
    const double cell_origin_x = largest_cx - max_xspan * 0.5;
    const double cell_origin_y = largest_cy - max_yspan * 0.5;
    const double cell_origin_z = largest_cz - max_zspan * 0.5;

    const int cells_toward_min_x = static_cast<int>(std::ceil((cell_origin_x - minX) / max_xspan));
    const int cells_toward_min_y = static_cast<int>(std::ceil((cell_origin_y - minY) / max_yspan));
    const int cells_toward_min_z = static_cast<int>(std::ceil((cell_origin_z - minZ) / max_zspan));
    const int cells_toward_max_x = static_cast<int>(std::ceil((maxX - cell_origin_x) / max_xspan));
    const int cells_toward_max_y = static_cast<int>(std::ceil((maxY - cell_origin_y) / max_yspan));
    const int cells_toward_max_z = static_cast<int>(std::ceil((maxZ - cell_origin_z) / max_zspan));

    Framework fw;
    fw.cell_size_x  = max_xspan;
    fw.cell_size_y  = max_yspan;
    fw.cell_size_z  = max_zspan;
    fw.num_cells_x  = cells_toward_min_x + cells_toward_max_x;
    fw.num_cells_y  = cells_toward_min_y + cells_toward_max_y;
    fw.num_cells_z  = cells_toward_min_z + cells_toward_max_z;
    fw.origin_x     = cell_origin_x - cells_toward_min_x * max_xspan;
    fw.origin_y     = cell_origin_y - cells_toward_min_y * max_yspan;
    fw.origin_z     = cell_origin_z - cells_toward_min_z * max_zspan;
    fw.rotation     = meta.rotation;

    std::cout << "MicromineReader: CellSize = ("
              << fw.cell_size_x << ", " << fw.cell_size_y << ", " << fw.cell_size_z << ")\n"
              << "                 Grid = " << fw.num_cells_x
              << " x " << fw.num_cells_y << " x " << fw.num_cells_z << "\n";

    // Recompute i/j/k and Morton keys now that CellSize is known
    rebuild_indices(model, fw);

    if (out_framework)
        *out_framework = fw;

    return model;
}

// ===========================================================================
// center_model
// ===========================================================================

void MicromineReader::center_model(BlockModelSoA& model)
{
    if (model.empty()) return;

    double sum_x = 0, sum_y = 0, sum_z = 0;
    size_t valid_count = 0;

    for (size_t idx = 0; idx < model.size(); ++idx)
    {
        if (std::isfinite(model.x[idx]) && std::isfinite(model.y[idx]) && std::isfinite(model.z[idx]))
        {
            sum_x += model.x[idx];
            sum_y += model.y[idx];
            sum_z += model.z[idx];
            ++valid_count;
        }
    }

    if (valid_count == 0) return;

    const double cx = sum_x / static_cast<double>(valid_count);
    const double cy = sum_y / static_cast<double>(valid_count);
    const double cz = sum_z / static_cast<double>(valid_count);

    for (size_t idx = 0; idx < model.size(); ++idx)
    {
        if (std::isfinite(model.x[idx])) model.x[idx] -= cx;
        if (std::isfinite(model.y[idx])) model.y[idx] -= cy;
        if (std::isfinite(model.z[idx])) model.z[idx] -= cz;
    }

    std::cout << "MicromineReader: centring shift = ("
              << cx << ", " << cy << ", " << cz << ")\n";
}

} // namespace Mining
