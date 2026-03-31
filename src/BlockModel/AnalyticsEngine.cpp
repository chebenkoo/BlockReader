#include "BlockModel/AnalyticsEngine.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Mining {

// Format a float value as a group key string.
// Integer-valued floats (100.0, -5.0) render without decimals.
// Others use up to 6 significant digits.
static std::string floatGroupKey(float v) {
    if (!std::isfinite(v)) return "<null>";
    float rounded = std::round(v * 1000.f) / 1000.f;
    if (rounded == std::floor(rounded) && std::abs(rounded) < 1e9f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(rounded));
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(rounded));
    return buf;
}

std::vector<SummaryRow> AnalyticsEngine::computeSummary(
    const BlockModelSoA& model,
    const std::string& groupingAttr,
    const std::string& gradeAttr,
    const std::string& densityAttr,
    const std::string& filterAttr,
    const std::string& filterValue
) {
    if (model.empty() || groupingAttr.empty()) return {};

    // Prefer string attribute; fall back to numeric (discrete codes, bench IDs, etc.)
    auto groupIt    = model.string_attributes.find(groupingAttr);
    auto groupNumIt = model.attributes.find(groupingAttr);
    if (groupIt == model.string_attributes.end() && groupNumIt == model.attributes.end()) return {};
    const bool numericGroup = (groupIt == model.string_attributes.end());

    const size_t n = model.size();

    // Filter setup (string attribute only)
    const BlockModelSoA::InternedString* filterVec = nullptr;
    if (!filterAttr.empty()) {
        auto fIt = model.string_attributes.find(filterAttr);
        if (fIt != model.string_attributes.end()) filterVec = &fIt->second;
    }

    // Grade / density pointers
    const std::vector<float>* gradeVec = nullptr;
    auto gradeIt = model.attributes.find(gradeAttr);
    if (gradeIt != model.attributes.end()) gradeVec = &gradeIt->second;

    const std::vector<float>* densityVec = nullptr;
    if (!densityAttr.empty()) {
        auto dIt = model.attributes.find(densityAttr);
        if (dIt != model.attributes.end()) densityVec = &dIt->second;
    }

    struct Acc {
        size_t count = 0;
        double vol = 0.0;
        double mass = 0.0;
        double weightedGrade = 0.0;
    };

    // ── String grouping path ────────────────────────────────────
    if (!numericGroup) {
        const auto& interned = groupIt->second;
        std::unordered_map<int32_t, Acc> accumulators;

        for (size_t i = 0; i < n; ++i) {
            if (filterVec) {
                int32_t fIdx = filterVec->indices[i];
                const std::string& val = (fIdx == -1) ? "" : filterVec->unique_values[fIdx];
                if (val != filterValue) continue;
            }

            int32_t gIdx = interned.indices[i];
            double v    = (double)model.x_span[i] * model.y_span[i] * model.z_span[i];
            double d    = densityVec ? (*densityVec)[i] : 1.0;
            double mass = v * d;
            double g    = gradeVec ? (*gradeVec)[i] : 0.0;

            auto& a = accumulators[gIdx];
            a.count++;
            a.vol          += v;
            a.mass         += mass;
            a.weightedGrade += (g * mass);
        }

        std::vector<SummaryRow> results;
        results.reserve(accumulators.size());
        for (auto const& [idx, a] : accumulators) {
            SummaryRow row;
            row.groupName = (idx == -1) ? "<Empty>" : interned.unique_values[idx];
            row.count     = a.count;
            row.volume    = a.vol;
            row.tonnes    = a.mass;
            row.metal     = a.weightedGrade;
            row.avgGrade  = (a.mass > 0) ? (a.weightedGrade / a.mass) : 0.0;
            results.push_back(std::move(row));
        }
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
            return a.volume > b.volume;
        });
        return results;
    }

    // ── Numeric grouping path (bench IDs, zone codes, etc.) ────
    const auto& numVec = groupNumIt->second;
    std::unordered_map<std::string, Acc> accumulators;

    for (size_t i = 0; i < n; ++i) {
        if (filterVec) {
            int32_t fIdx = filterVec->indices[i];
            const std::string& val = (fIdx == -1) ? "" : filterVec->unique_values[fIdx];
            if (val != filterValue) continue;
        }

        std::string key = floatGroupKey(numVec[i]);
        double v    = (double)model.x_span[i] * model.y_span[i] * model.z_span[i];
        double d    = densityVec ? (*densityVec)[i] : 1.0;
        double mass = v * d;
        double g    = gradeVec ? (*gradeVec)[i] : 0.0;

        auto& a = accumulators[key];
        a.count++;
        a.vol          += v;
        a.mass         += mass;
        a.weightedGrade += (g * mass);
    }

    std::vector<SummaryRow> results;
    results.reserve(accumulators.size());
    for (auto const& [key, a] : accumulators) {
        SummaryRow row;
        row.groupName = key;
        row.count     = a.count;
        row.volume    = a.vol;
        row.tonnes    = a.mass;
        row.metal     = a.weightedGrade;
        row.avgGrade  = (a.mass > 0) ? (a.weightedGrade / a.mass) : 0.0;
        results.push_back(std::move(row));
    }

    // Sort by Volume descending
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.volume > b.volume;
    });

    return results;
}

} // namespace Mining
