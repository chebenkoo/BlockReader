#pragma once

#include "BlockModel.h"
#include <vector>
#include <string>

namespace Mining {

/**
 * @brief Parameters for block economic valuation.
 */
struct EconomicParams {
    float gold_price = 2000.0f;      // $/oz
    float recovery = 0.90f;          // 0.0 - 1.0
    float mining_cost = 2.50f;       // $/tonne
    float processing_cost = 15.0f;   // $/tonne
    std::string grade_attr = "Grade";
    std::string density_attr = "Density";
};

/**
 * @brief Core engine for Phase 2: Geometric Intelligence & Pit Optimization.
 */
class Optimizer {
public:
    /**
     * @brief Calculates the net value of every block in the model.
     * Value = (Revenue - ProcessingCost) if Ore, else -MiningCost.
     */
    static std::vector<float> calculateBlockValues(const BlockModelSoA& model, const EconomicParams& params);

    /**
     * @brief Generates block-on-block dependencies (precedence) for a given slope angle.
     * Uses the i,j,k indices for fast neighborhood lookup.
     */
    static std::vector<std::vector<size_t>> generatePrecedence(const BlockModelSoA& model, float slope_angle_deg = 45.0f);

    /**
     * @brief Runs the Pit Optimization (Maximum Weight Closure).
     * Updates the mined_state in the model.
     */
    static void runPitOptimization(BlockModelSoA& model, const EconomicParams& params);
};

} // namespace Mining
