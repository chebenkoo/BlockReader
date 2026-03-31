#pragma once

#include "BlockModel.h"
#include <string>
#include <vector>
#include <variant>

namespace Mining {

/**
 * @brief Represents a single row of summary statistics (e.g., for a specific RockType).
 */
struct SummaryRow {
    std::string groupName;
    size_t count = 0;
    double volume = 0.0;
    double tonnes = 0.0;
    double avgGrade = 0.0;
    double metal = 0.0;
};

/**
 * @brief High-performance aggregation engine for BlockModel analytics.
 */
class AnalyticsEngine {
public:
    /**
     * @brief Computes summary statistics grouped by a categorical attribute.
     * @param model The BlockModelSoA to analyze.
     * @param groupingAttr Name of the categorical (string) attribute for grouping.
     * @param gradeAttr Name of the numeric attribute to average.
     * @param densityAttr Optional name of a numeric attribute for density. If empty, uses 1.0.
     * @return Vector of summary rows.
     */
    static std::vector<SummaryRow> computeSummary(
        const BlockModelSoA& model,
        const std::string& groupingAttr,
        const std::string& gradeAttr,
        const std::string& densityAttr = "",
        const std::string& filterAttr = "",
        const std::string& filterValue = ""
    );
};

} // namespace Mining
