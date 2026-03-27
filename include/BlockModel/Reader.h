#pragma once

#include "BlockModel.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace Mining {

/**
 * @brief Configuration for mapping CSV columns to BlockModel attributes.
 */
struct ColumnMapping {
    // 1. Required Spatial Columns (CSV headers)
    std::string x_col = "x";
    std::string y_col = "y";
    std::string z_col = "z";

    // 2. Optional Grid Spans/Sizes
    std::string x_span_col = "x_span";
    std::string y_span_col = "y_span";
    std::string z_span_col = "z_span";

    // 3. Optional Grid Indices
    std::string i_col = "i";
    std::string j_col = "j";
    std::string k_col = "k";

    // 3. Flexible Attribute Mapping
    // Key: Internal Name (e.g., "Grade"), Value: CSV Header (e.g., "Au_gpt")
    std::map<std::string, std::string> attribute_map = {
        {"Grade", "au"},
        {"Density", "density"},
        {"RockType", "rocktype"}
    };

    // 4. Normalization
    double offset_x = 0.0;
    double offset_y = 0.0;
    double offset_z = 0.0;
};

/**
 * @brief High-performance CSV/DAT Block Model Reader.
 */
class Reader {
public:
    struct Progress {
        size_t current_row;
        size_t total_rows;
        std::string message;
    };

    using ProgressCallback = std::function<void(const Progress&)>;

    /**
     * @brief Loads a block model from a CSV file.
     * @param file_path Path to the .csv or .dat file.
     * @param mapping Mapping of header names to attributes.
     * @param callback Optional progress callback for UI.
     * @return Loaded BlockModelSoA.
     */
    static BlockModelSoA load_from_csv(
        const std::string& file_path, 
        const ColumnMapping& mapping,
        ProgressCallback callback = nullptr
    );

    /**
     * @brief Loads a block model into an AoS structure.
     */
    static BlockModelAoS load_to_aos(
        const std::string& file_path, 
        const ColumnMapping& mapping,
        ProgressCallback callback = nullptr
    );

private:
    static std::vector<std::string> split_line(const std::string& line, char delimiter);
};

} // namespace Mining
