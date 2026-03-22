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
class MicromineReader {
public:
    struct Variable {
        std::string name;
        char type; // 'R' = Real (8 bytes), 'C' = Char
        int size;
        int precision;
    };

    struct MetaData {
        double origin_x = 0, origin_y = 0, origin_z = 0;
        double pivot_x = 0, pivot_y = 0, pivot_z = 0;
        double rotation_z = 0;
        double block_x = 10, block_y = 10, block_z = 5;
    };

    /**
     * @brief Only reads the header (XML + VARIABLES) to get field names.
     */
    static std::vector<Variable> get_variables(const std::string& file_path, MetaData& out_meta);

    /**
     * @brief Full load with user-provided field mapping.
     */
    static BlockModelSoA load(const std::string& file_path, const std::map<std::string, std::string>& mapping);

    /**
     * @brief Centers the model at (0,0,0) for maximum precision.
     */
    static void center_model(BlockModelSoA& model);
};

} // namespace Mining
