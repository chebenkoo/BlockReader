#include "BlockModel/Optimizer.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace Mining {

std::vector<float> Optimizer::calculateBlockValues(const BlockModelSoA& model, const EconomicParams& params) {
    size_t n = model.size();
    std::vector<float> values(n, 0.0f);

    const std::vector<float>* grade = nullptr;
    auto it_g = model.attributes.find(params.grade_attr);
    if (it_g != model.attributes.end()) grade = &it_g->second;

    const std::vector<float>* density = nullptr;
    auto it_d = model.attributes.find(params.density_attr);
    if (it_d != model.attributes.end()) density = &it_d->second;

    if (!grade) {
        std::cerr << "Optimizer Error: Grade attribute '" << params.grade_attr << "' not found." << std::endl;
        return values;
    }

    for (size_t i = 0; i < n; ++i) {
        float dens = (density && i < density->size()) ? (*density)[i] : 2.5f;
        float mass = model.x_span[i] * 2.0f * model.y_span[i] * 2.0f * model.z_span[i] * 2.0f * dens;
        
        float g = (*grade)[i];
        
        // Revenue per tonne (assuming grade is in g/t or similar for Gold)
        // Adjust units based on user mapping if necessary
        float revenue = (g * params.recovery * (params.gold_price / 31.1035f)); // 31.1035g per oz
        
        float net_processing = revenue - params.processing_cost;
        
        if (net_processing > 0) {
            // Ore Block: We gain net_processing but we still pay mining cost
            values[i] = (net_processing - params.mining_cost) * mass;
        } else {
            // Waste Block: We only pay mining cost
            values[i] = -params.mining_cost * mass;
        }
    }

    return values;
}

std::vector<std::vector<size_t>> Optimizer::generatePrecedence(const BlockModelSoA& model, float slope_angle_deg) {
    // Placeholder for geometric cone search using CGAL AABB tree
    // To be implemented in Task 2.1
    return {};
}

void Optimizer::runPitOptimization(BlockModelSoA& model, const EconomicParams& params) {
    std::cout << "Optimizer: Starting Pit Optimization..." << std::endl;
    
    // 1. Calculate Values
    auto values = calculateBlockValues(model, params);
    
    // 2. Generate Graph (Precedence)
    auto dependencies = generatePrecedence(model);
    
    // 3. Solve (Pseudoflow)
    // To be implemented in Task 3.1
    
    std::cout << "Optimizer: Pit Optimization Complete." << std::endl;
}

} // namespace Mining
