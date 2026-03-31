#pragma once

#include "BlockModel.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace Mining {

/**
 * @brief Evaluates mathematical formulas for BlockModel attributes.
 * Uses ExprTk for high-performance batch evaluation.
 */
class FormulaEvaluator {
public:
    struct Formula {
        std::string name;       // Name of the resulting attribute (e.g., "Volume")
        std::string expression; // Formula (e.g., "XSPAN * YSPAN * ZSPAN * Density")
    };

    /**
     * @brief Evaluates a set of formulas and adds the results to the model.
     * @param model The BlockModelSoA to update.
     * @param formulas List of formulas to evaluate.
     * @return Error message if any, empty string on success.
     */
    static std::string evaluate(BlockModelSoA& model, const std::vector<Formula>& formulas);
};

} // namespace Mining
