#include "BlockModel/AnalyticsEngine.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace Mining {

std::vector<SummaryRow> AnalyticsEngine::computeSummary(
    const BlockModelSoA& model,
    const std::string& groupingAttr,
    const std::string& gradeAttr,
    const std::string& densityAttr,
    const std::string& filterAttr,
    const std::string& filterValue
) {
    if (model.empty() || groupingAttr.empty()) return {};

    auto groupIt = model.string_attributes.find(groupingAttr);
    if (groupIt == model.string_attributes.end()) return {};

    const auto& interned = groupIt->second;
    const size_t n = model.size();

    // Setup filter pointers
    const BlockModelSoA::InternedString* filterVec = nullptr;
    if (!filterAttr.empty()) {
        auto fIt = model.string_attributes.find(filterAttr);
        if (fIt != model.string_attributes.end()) filterVec = &fIt->second;
    }

    // Setup attribute pointers for hot loop
    const std::vector<float>* gradeVec = nullptr;
    auto gradeIt = model.attributes.find(gradeAttr);
    if (gradeIt != model.attributes.end()) gradeVec = &gradeIt->second;

    const std::vector<float>* densityVec = nullptr;
    if (!densityAttr.empty()) {
        auto dIt = model.attributes.find(densityAttr);
        if (dIt != model.attributes.end()) densityVec = &dIt->second;
    }

    // Temporary accumulation (by group index)
    struct Acc { 
        size_t count = 0; 
        double vol = 0.0; 
        double mass = 0.0; 
        double weightedGrade = 0.0; 
    };
    std::unordered_map<int32_t, Acc> accumulators;

    for (size_t i = 0; i < n; ++i) {
        // Apply filter
        if (filterVec) {
            int32_t fIdx = filterVec->indices[i];
            const std::string& val = (fIdx == -1) ? "" : filterVec->unique_values[fIdx];
            if (val != filterValue) continue;
        }

        int32_t gIdx = interned.indices[i];
        
        double v = (double)model.x_span[i] * model.y_span[i] * model.z_span[i];
        double d = densityVec ? (*densityVec)[i] : 1.0;
        double mass = v * d;
        double g = gradeVec ? (*gradeVec)[i] : 0.0;

        auto& a = accumulators[gIdx];
        a.count++;
        a.vol += v;
        a.mass += mass;
        a.weightedGrade += (g * mass);
    }

    // Convert accumulators to sorted result
    std::vector<SummaryRow> results;
    results.reserve(accumulators.size());

    for (auto const& [idx, a] : accumulators) {
        SummaryRow row;
        row.groupName = (idx == -1) ? "<Empty>" : interned.unique_values[idx];
        row.count = a.count;
        row.volume = a.vol;
        row.tonnes = a.mass;
        row.metal = a.weightedGrade;
        row.avgGrade = (a.mass > 0) ? (a.weightedGrade / a.mass) : 0.0;
        results.push_back(std::move(row));
    }

    // Sort by Volume descending
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.volume > b.volume;
    });

    return results;
}

} // namespace Mining
