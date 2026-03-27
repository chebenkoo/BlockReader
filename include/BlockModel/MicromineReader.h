#pragma once

#include "BlockModel.h"
#include <string>
#include <vector>
#include <map>

namespace Mining {

/**
 * @brief Reader for Micromine Extended Data Files (.DAT).
 * Handles XML metadata headers and binary block records.
 */
class MicromineReader
{
public:
    // -----------------------------------------------------------------------
    // Rotation metadata read from the XML header
    // -----------------------------------------------------------------------
    struct RotationData
    {
        double rotation_x_deg = 0.0;   // rotation around X axis
        double rotation_y_deg = 0.0;   // rotation around Y axis
        double rotation_z_deg = 0.0;   // rotation around Z axis
        double pivot_x        = 0.0;   // point of rotation X
        double pivot_y        = 0.0;   // point of rotation Y
        double pivot_z        = 0.0;   // point of rotation Z
        bool   is_rotated     = false;

        // Build the model-space → world-space matrix.
        Mat4 build_model_to_world() const;
    };

    // -----------------------------------------------------------------------
    // File metadata extracted from the binary header
    // -----------------------------------------------------------------------
    struct MetaData
    {
        double origin_x = 0.0;   // model-space origin (from XML)
        double origin_y = 0.0;
        double origin_z = 0.0;
        RotationData rotation;
    };

    // -----------------------------------------------------------------------
    // Variable descriptor
    // -----------------------------------------------------------------------
    struct Variable
    {
        std::string name;
        char        type      = 'R'; 
        int         size      = 8;   
        int         precision = 0;   
    };

    // -----------------------------------------------------------------------
    // Parsed grid framework
    // -----------------------------------------------------------------------
    struct Framework
    {
        double origin_x = 0, origin_y = 0, origin_z = 0;
        double cell_size_x = 1, cell_size_y = 1, cell_size_z = 1;
        int    num_cells_x = 0, num_cells_y = 0, num_cells_z = 0;
        RotationData rotation;
    };

    /// Returns variable descriptors and fills out_meta from the file header.
    static std::vector<Variable> get_variables(const std::string& file_path,
                                               MetaData& out_meta);

    /// Reads the full block model into a BlockModelSoA.
    static BlockModelSoA load(const std::string& file_path,
                              const std::map<std::string, std::string>& mapping,
                              Framework* out_framework = nullptr);

    /// Recentres all block centroids around the mean position.
    static void center_model(BlockModelSoA& model);

private:
    static constexpr size_t PageSize = 4096;

    static size_t parse_header(std::ifstream& file,
                               MetaData& meta,
                               std::vector<Variable>& vars);

    static size_t record_stride(const std::vector<Variable>& vars);

    static int detect_status_byte(std::ifstream& file,
                                  size_t data_start,
                                  const std::vector<Variable>& vars,
                                  const std::string& x_col,
                                  const std::string& y_col,
                                  const MetaData& meta,
                                  size_t& out_sync_start);

    static double read_field_double(const char* src, const Variable& var);

    static void rebuild_indices(BlockModelSoA& model, const Framework& fw);
};

} // namespace Mining
