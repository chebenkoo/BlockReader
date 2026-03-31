#include "BlockModel/FormulaEvaluator.h"
#include <iostream>
#include <algorithm>
#include <map>

// Note: Ensure exprtk.hpp is in third_party/ or on the include path.
// This header is very large, so we include it only in the .cpp.
#if __has_include("exprtk.hpp")
#include "exprtk.hpp"
#define HAS_EXPRTK
#elif __has_include("../third_party/exprtk.hpp")
#include "../third_party/exprtk.hpp"
#define HAS_EXPRTK
#endif

namespace Mining {

#ifdef HAS_EXPRTK
std::string FormulaEvaluator::evaluate(BlockModelSoA& model, const std::vector<Formula>& formulas) {
    if (model.empty() || formulas.empty()) return "";

    const size_t n = model.size();

    // Map attribute names to vectors to expose them as variables.
    // We must ensure the names in the formula match our internal names.
    typedef exprtk::symbol_table<float> symbol_table_t;
    typedef exprtk::expression<float>   expression_t;
    typedef exprtk::parser<float>       parser_t;

    for (const auto& formula : formulas) {
        if (formula.name.empty() || formula.expression.empty()) continue;

        symbol_table_t symbol_table;
        
        // Define common variables (scalars used in a loop for per-block eval)
        float x, y, z, xs, ys, zs;
        symbol_table.add_variable("X", x);
        symbol_table.add_variable("Y", y);
        symbol_table.add_variable("Z", z);
        symbol_table.add_variable("XSPAN", xs);
        symbol_table.add_variable("YSPAN", ys);
        symbol_table.add_variable("ZSPAN", zs);

        // Add all existing attributes as variables
        std::map<std::string, float> attr_values;
        for (auto& [name, vec] : model.attributes) {
            attr_values[name] = 0.0f;
            symbol_table.add_variable(name, attr_values[name]);
        }

        expression_t expression;
        expression.register_symbol_table(symbol_table);

        parser_t parser;
        if (!parser.compile(formula.expression, expression)) {
            std::string err = "ExprTk compile error for '" + formula.name + "': " + parser.error();
            std::cerr << err << std::endl;
            return err;
        }

        // Pre-fetch attribute vector pointers to avoid map lookups in the hot loop
        struct AttrBinding { float* target_val; const std::vector<float>* source_vec; };
        std::vector<AttrBinding> bindings;
        for (auto& [name, vec] : model.attributes) {
            if (name == formula.name) continue; // Don't bind to the result if it exists
            bindings.push_back({&attr_values[name], &vec});
        }

        // Allocate result vector
        std::vector<float> result(n);

        // Evaluate for every block
        for (size_t i = 0; i < n; ++i) {
            x = model.x[i]; y = model.y[i]; z = model.z[i];
            xs = model.x_span[i]; ys = model.y_span[i]; zs = model.z_span[i];

            for (auto& binding : bindings) {
                *binding.target_val = (*binding.source_vec)[i];
            }

            result[i] = expression.value();
        }

        // Add or replace the attribute in the model
        model.attributes[formula.name] = std::move(result);
        
        // Calculate ranges for the new attribute
        float minV = 1e30f, maxV = -1e30f;
        bool found = false;
        for (float v : model.attributes[formula.name]) {
            if (std::isfinite(v)) {
                minV = std::min(minV, v);
                maxV = std::max(maxV, v);
                found = true;
            }
        }
        if (found) model.attribute_ranges[formula.name] = {minV, maxV};
    }

    return "";
}
#else
std::string FormulaEvaluator::evaluate(BlockModelSoA& model, const std::vector<Formula>& formulas) {
    // Basic fallback or error if ExprTk is missing.
    // In a real project, we'd ensure the dependency is met.
    return "Error: exprtk.hpp not found in third_party/. Please add it to enable formula calculation.";
}
#endif

} // namespace Mining
