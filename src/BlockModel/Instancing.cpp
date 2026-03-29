#include "BlockModel/Instancing.h"
#include <QtGui/qvector3d.h>
#include <QtGui/qvector4d.h>
#include <QtGui/qquaternion.h>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Mining {


BlockModelProvider::BlockModelProvider(QQuick3DObject *parent) : QQuick3DInstancing(parent) {}
BlockModelProvider::~BlockModelProvider() = default;

void BlockModelProvider::autoComputeRange() {
    if (!m_model || m_model->attributes.empty()) return;

    // 1. Calculate range for Color Attribute
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end() && !it->second.empty()) {
        const auto& vec = it->second;
        float min = 1e30f, max = -1e30f;
        bool found = false;
        for (float v : vec) {
            if (std::isfinite(v)) { min = std::min(min, v); max = std::max(max, v); found = true; }
        }
        if (found) { m_minRange = min; m_maxRange = (max > min) ? max : min + 1.0f; emit rangeChanged(); }
    }

    // 2. Calculate max for 'Grade' filtering slider
    auto git = m_model->attributes.find("Grade");
    if (git != m_model->attributes.end() && !git->second.empty()) {
        float gMax = -1e30f;
        bool gFound = false;
        for (float v : git->second) {
            if (std::isfinite(v)) { gMax = std::max(gMax, v); gFound = true; }
        }
        if (gFound && m_gradeMax != gMax) { m_gradeMax = gMax; emit gradeMaxChanged(); }
    } else if (it != m_model->attributes.end() && m_gradeMax != m_maxRange) {
        // Fallback to current color attribute if "Grade" is missing
        m_gradeMax = m_maxRange;
        emit gradeMaxChanged();
    }
}

void BlockModelProvider::setModel(const BlockModelSoA* model) { m_model = model; autoComputeRange(); markDirty(); }
void BlockModelProvider::setColorAttribute(const QString &attr) { if (m_colorAttribute != attr) { m_colorAttribute = attr; autoComputeRange(); emit colorAttributeChanged(); markDirty(); } }
void BlockModelProvider::setMinRange(float val) { m_minRange = val; emit rangeChanged(); markDirty(); }
void BlockModelProvider::setMaxRange(float val) { m_maxRange = val; emit rangeChanged(); markDirty(); }
void BlockModelProvider::setMinGrade(float val) { m_minGrade = val; emit minGradeChanged(); markDirty(); }
void BlockModelProvider::setBlockSize(float val) { if (m_blockSize != val) { m_blockSize = val; emit blockSizeChanged(); markDirty(); } }

void BlockModelProvider::setGridMode(bool val) {
    if (m_gridMode != val) {
        m_gridMode = val;
        emit gridModeChanged();
        markDirty();
    }
}

void BlockModelProvider::setModelRotation(float rx, float ry, float rz) { m_rotX = rx; m_rotY = ry; m_rotZ = rz; markDirty(); }

QByteArray BlockModelProvider::getInstanceBuffer(int *instanceCount) {
    if (!m_model || m_model->empty()) {
        *instanceCount = 0;
        return QByteArray();
    }

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::milliseconds;
    const auto t0 = Clock::now();

    const size_t count = m_model->size();
    QByteArray instanceData;
    instanceData.resize(count * sizeof(InstanceTableEntry));
    auto* entry = reinterpret_cast<InstanceTableEntry*>(instanceData.data());

    const std::vector<float>* attrValues = nullptr;
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end()) {
        attrValues = &it->second;
    }

    const std::vector<float>* gradeValues = nullptr;
    auto git = m_model->attributes.find("Grade");
    if (git != m_model->attributes.end()) {
        gradeValues = &git->second;
    } else {
        gradeValues = attrValues;
    }

    m_instanceToModelIndex.clear();
    m_instanceToModelIndex.reserve(count);

    // Derive fallback span
    float fallback_sx = 10.0f, fallback_sy = 5.0f, fallback_sz = 10.0f;
    for (size_t fi = 0; fi < m_model->x_span.size(); ++fi) {
        if (m_model->x_span[fi] > 0.01f) {
            fallback_sx = m_model->x_span[fi];
            fallback_sy = (m_model->z_span.size() > fi) ? m_model->z_span[fi] : fallback_sx;
            fallback_sz = (m_model->y_span.size() > fi) ? m_model->y_span[fi] : fallback_sx;
            break;
        }
    }

    const float baseScale = m_blockSize / 100.0f;
    const float visualScale = m_gridMode ? 0.95f : 1.0f;
    const float finalScaleFactor = baseScale * visualScale;

    const float range = m_maxRange - m_minRange;
    const float invRange = (std::abs(range) > 0.0001f) ? 1.0f / range : 0.0f;

    // Pre-compute rotation matrix from Euler angles ONCE (not 2.7M times)
    const QQuaternion q = QQuaternion::fromEulerAngles(m_rotX, m_rotY, m_rotZ).normalized();
    const float qw = q.scalar(), qx = q.x(), qy = q.y(), qz = q.z();
    const float R00 = 1.f - 2.f*(qy*qy + qz*qz);
    const float R01 = 2.f*(qx*qy - qw*qz);
    const float R02 = 2.f*(qx*qz + qw*qy);
    const float R10 = 2.f*(qx*qy + qw*qz);
    const float R11 = 1.f - 2.f*(qx*qx + qz*qz);
    const float R12 = 2.f*(qy*qz - qw*qx);
    const float R20 = 2.f*(qx*qz - qw*qy);
    const float R21 = 2.f*(qy*qz + qw*qx);
    const float R22 = 1.f - 2.f*(qx*qx + qy*qy);

    // Use raw pointers for maximum performance
    const float* px = m_model->x.data();
    const float* py = m_model->y.data();
    const float* pz = m_model->z.data();
    const float* psx = m_model->x_span.data();
    const float* psy = m_model->y_span.data();
    const float* psz = m_model->z_span.data();
    const uint8_t* pvis = m_model->visible.empty() ? nullptr : m_model->visible.data();
    const float* pattr = attrValues ? attrValues->data() : nullptr;
    const float* pgrade = gradeValues ? gradeValues->data() : nullptr;

    const size_t xspan_size = m_model->x_span.size();
    const size_t yspan_size = m_model->y_span.size();
    const size_t zspan_size = m_model->z_span.size();

    int addedCount = 0;
    for (size_t i = 0; i < count; ++i) {
        // 1. Skip invalid or invisible blocks
        if (!std::isfinite(px[i]) || !std::isfinite(py[i]) || !std::isfinite(pz[i])) continue;
        if (pvis && pvis[i] == 0) continue;

        // 2. Grade filtering
        const float gradeVal = pgrade ? pgrade[i] : 0.0f;
        if (std::isfinite(gradeVal) && gradeVal < m_minGrade) continue;

        // 3. Position (Z-up → Y-up)
        const float tx = px[i];
        const float ty = pz[i];
        const float tz = -py[i];

        // 4. Per-block scale (already includes finalScaleFactor)
        const float sx = ((psx && i < xspan_size && psx[i] > 0.01f) ? psx[i] : fallback_sx) * finalScaleFactor;
        const float sy = ((psz && i < zspan_size && psz[i] > 0.01f) ? psz[i] : fallback_sy) * finalScaleFactor;
        const float sz = ((psy && i < yspan_size && psy[i] > 0.01f) ? psy[i] : fallback_sz) * finalScaleFactor;

        // 5. Directly pack TRS matrix (T*R*S, row-major) — no QMatrix4x4, no function call
        auto& e = entry[addedCount];
        e.row0 = QVector4D(sx*R00, sy*R01, sz*R02, tx);
        e.row1 = QVector4D(sx*R10, sy*R11, sz*R12, ty);
        e.row2 = QVector4D(sx*R20, sy*R21, sz*R22, tz);

        // 6. Inline HSV→RGB (S=1, V=1, H in [0, 0.66]) — no QColor construction
        const float val = pattr ? pattr[i] : 0.0f;
        const float t = std::clamp((val - m_minRange) * invRange, 0.0f, 1.0f);
        const float h6 = (1.0f - t) * 3.96f; // 0.66 * 6
        const int sector = static_cast<int>(h6);
        const float frac = h6 - sector;
        float r, g, b;
        switch (sector) {
            case 0:  r = 1.f;       g = frac;       b = 0.f; break;
            case 1:  r = 1.f-frac;  g = 1.f;        b = 0.f; break;
            case 2:  r = 0.f;       g = 1.f;        b = frac; break;
            default: r = 0.f;       g = 1.f-frac;   b = 1.f; break;
        }
        e.color = QVector4D(r, g, b, 1.0f);
        e.instanceData = QVector4D(val, 0.f, 0.f, 0.f);

        m_instanceToModelIndex.push_back(static_cast<int>(i));
        ++addedCount;
    }

    *instanceCount = addedCount;
    instanceData.resize(addedCount * sizeof(InstanceTableEntry));
    qDebug() << "[RENDER] getInstanceBuffer END —" << addedCount << "/" << count
             << "blocks in" << std::chrono::duration_cast<Ms>(Clock::now() - t0).count() << "ms";
    return instanceData;
}

QVariantMap BlockModelProvider::getBlockInfo(int instanceIndex) const {
    QVariantMap result;
    if (!m_model || instanceIndex < 0 || instanceIndex >= (int)m_instanceToModelIndex.size())
        return result;
    int i = m_instanceToModelIndex[instanceIndex];
    result["_index"]  = i;
    result["X"]       = m_model->x[i];
    result["Y"]       = m_model->y[i];
    result["Z"]       = m_model->z[i];
    result["X_span"]  = (i < (int)m_model->x_span.size()) ? m_model->x_span[i] : 0.f;
    result["Y_span"]  = (i < (int)m_model->y_span.size()) ? m_model->y_span[i] : 0.f;
    result["Z_span"]  = (i < (int)m_model->z_span.size()) ? m_model->z_span[i] : 0.f;
    for (auto const& [k, v] : m_model->attributes) {
        if (i < (int)v.size())
            result[QString::fromStdString(k)] = v[i];
    }
    for (auto const& [k, v] : m_model->string_attributes) {
        if (i < (int)v.size())
            result[QString::fromStdString(k)] = QString::fromStdString(v[i]);
    }
    return result;
}

QColor BlockModelProvider::mapValueToColor(float value) const {
    float range = m_maxRange - m_minRange;
    if (std::abs(range) < 0.0001f) return Qt::red;
    float t = std::clamp((value - m_minRange) / range, 0.0f, 1.0f);
    return QColor::fromHsvF((1.0f - t) * 0.66f, 1.0, 1.0);
}

} // namespace Mining
