#include "BlockModel/Instancing.h"
#include <QtGui/qvector3d.h>
#include <QtGui/qvector4d.h>
#include <QtGui/qquaternion.h>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include "BlockModel/ModelDiagnostics.h"

namespace Mining {

BlockModelProvider::BlockModelProvider(QQuick3DObject *parent) : QQuick3DInstancing(parent) {}
BlockModelProvider::~BlockModelProvider() = default;

QString BlockModelProvider::colorAttribute() const {
    std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
    return QString::fromStdString(m_colorAttribute);
}

void BlockModelProvider::setModel(const BlockModelSoA* model) {
    bool rangeChanged_sig = false;
    bool gradeMaxChanged_sig = false;

    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        m_model = model;
        m_cachedColumns.clear();

        if (m_model) {
            // 1. Set range for current Color Attribute from pre-calculated data
            auto it = m_model->attribute_ranges.find(m_colorAttribute);
            if (it != m_model->attribute_ranges.end()) {
                m_minRange = it->second.first;
                m_maxRange = it->second.second;
                rangeChanged_sig = true;
            }

            // 2. Set max for 'Grade' filtering slider
            auto git = m_model->attribute_ranges.find("Grade");
            if (git != m_model->attribute_ranges.end()) {
                if (m_gradeMax != git->second.second) {
                    m_gradeMax = git->second.second;
                    gradeMaxChanged_sig = true;
                }
            } else if (it != m_model->attribute_ranges.end() && m_gradeMax != m_maxRange) {
                m_gradeMax = m_maxRange;
                gradeMaxChanged_sig = true;
            }

            // 3. Pre-convert all column names to QStrings once.
            //    getBlockInfo iterates this cache — no QString::fromStdString on each click.
            m_cachedColumns.reserve(m_model->attributes.size() + m_model->string_attributes.size());
            for (auto const& [name, vec] : m_model->attributes)
                m_cachedColumns.push_back({ QString::fromStdString(name), &vec, nullptr });
            for (auto const& [name, interned] : m_model->string_attributes)
                m_cachedColumns.push_back({ QString::fromStdString(name), nullptr, &interned });
        }
    }

    if (rangeChanged_sig) emit rangeChanged();
    if (gradeMaxChanged_sig) emit gradeMaxChanged();
    markDirty();
}

void BlockModelProvider::setColorAttribute(const QString &attr) {
    std::string sAttr = attr.toStdString();
    bool changed = false;
    bool rangeChanged_sig = false;
    bool gradeMaxChanged_sig = false;

    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        if (m_colorAttribute != sAttr) {
            m_colorAttribute = sAttr;
            changed = true;

            if (m_model) {
                auto it = m_model->attribute_ranges.find(m_colorAttribute);
                if (it != m_model->attribute_ranges.end()) {
                    // Numeric attribute: use pre-computed range
                    m_minRange = it->second.first;
                    m_maxRange = it->second.second;
                    rangeChanged_sig = true;
                    if (m_model->attribute_ranges.find("Grade") == m_model->attribute_ranges.end()) {
                        m_gradeMax = m_maxRange;
                        gradeMaxChanged_sig = true;
                    }
                } else {
                    // String (categorical) attribute: range = [0, uniqueCount-1]
                    auto sit = m_model->string_attributes.find(m_colorAttribute);
                    if (sit != m_model->string_attributes.end()) {
                        m_minRange = 0.0f;
                        m_maxRange = static_cast<float>(
                            sit->second.unique_values.empty() ? 1
                            : sit->second.unique_values.size() - 1);
                        rangeChanged_sig = true;
                    }
                }
            }
        }
    }

    if (changed) emit colorAttributeChanged();
    if (rangeChanged_sig) emit rangeChanged();
    if (gradeMaxChanged_sig) emit gradeMaxChanged();
    if (changed) markDirty();
}

void BlockModelProvider::setMinRange(float val) {
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        m_minRange = val;
    }
    emit rangeChanged();
    markDirty();
}

void BlockModelProvider::setMaxRange(float val) {
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        m_maxRange = val;
    }
    emit rangeChanged();
    markDirty();
}

void BlockModelProvider::setMinGrade(float val) {
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        m_minGrade = val;
    }
    emit minGradeChanged();
    markDirty();
}

void BlockModelProvider::setBlockSize(float val) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        if (m_blockSize != val) {
            m_blockSize = val;
            changed = true;
        }
    }
    if (changed) {
        emit blockSizeChanged();
        markDirty();
    }
}

void BlockModelProvider::setGridMode(bool val) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        if (m_gridMode != val) {
            m_gridMode = val;
            changed = true;
        }
    }
    if (changed) {
        emit gridModeChanged();
        markDirty();
    }
}

void BlockModelProvider::setModelRotation(float rx, float ry, float rz) {
    {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        m_rotX = rx; m_rotY = ry; m_rotZ = rz;
    }
    markDirty();
}

QByteArray BlockModelProvider::getInstanceBuffer(int *instanceCount) {
    qDebug() << "[RENDER] getInstanceBuffer START — process:" << ModelDiagnostics::processMemoryMB() << "MB";
    std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());

    if (!m_model || m_model->empty()) {
        *instanceCount = 0;
        return QByteArray();
    }

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::milliseconds;
    const auto t0 = Clock::now();

    const size_t count = m_model->size();
    QByteArray instanceData;
    qDebug() << "[RENDER] resizing instanceData to" << (count * sizeof(InstanceTableEntry)) / (1024*1024) << "MB...";
    instanceData.resize(static_cast<qsizetype>(count * sizeof(InstanceTableEntry)));
    qDebug() << "[RENDER] resized. Process:" << ModelDiagnostics::processMemoryMB() << "MB";
    auto* entry = reinterpret_cast<InstanceTableEntry*>(instanceData.data());

    // Numeric colour attribute
    const std::vector<float>* attrValues = nullptr;
    auto it = m_model->attributes.find(m_colorAttribute);
    if (it != m_model->attributes.end())
        attrValues = &it->second;

    // Categorical (string-interned) colour attribute
    const BlockModelSoA::InternedString* strAttr   = nullptr;
    int                                  strUnique  = 0;
    if (!attrValues) {
        auto sit = m_model->string_attributes.find(m_colorAttribute);
        if (sit != m_model->string_attributes.end()) {
            strAttr   = &sit->second;
            strUnique = static_cast<int>(sit->second.unique_values.size());
        }
    }

    const std::vector<float>* gradeValues = nullptr;
    auto git = m_model->attributes.find("Grade");
    if (git != m_model->attributes.end()) {
        gradeValues = &git->second;
    } else {
        gradeValues = attrValues;   // fall back to colour attr if Grade missing
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
        if (!std::isfinite(px[i]) || !std::isfinite(py[i]) || !std::isfinite(pz[i])) continue;
        if (pvis && pvis[i] == 0) continue;

        const float gradeVal = pgrade ? pgrade[i] : 0.0f;
        if (std::isfinite(gradeVal) && gradeVal < m_minGrade) continue;

        const float tx = px[i];
        const float ty = pz[i];
        const float tz = -py[i];

        const float sx = ((psx && i < xspan_size && psx[i] > 0.01f) ? psx[i] : fallback_sx) * finalScaleFactor;
        const float sy = ((psz && i < zspan_size && psz[i] > 0.01f) ? psz[i] : fallback_sy) * finalScaleFactor;
        const float sz = ((psy && i < yspan_size && psy[i] > 0.01f) ? psy[i] : fallback_sz) * finalScaleFactor;

        auto& e = entry[addedCount];
        e.row0 = QVector4D(sx*R00, sy*R01, sz*R02, tx);
        e.row1 = QVector4D(sx*R10, sy*R11, sz*R12, ty);
        e.row2 = QVector4D(sx*R20, sy*R21, sz*R22, tz);

        float r, g, b;
        float val = 0.0f;

        if (strAttr) {
            // Categorical: map category index → evenly-spaced hue
            int32_t catIdx = (i < strAttr->indices.size()) ? strAttr->indices[i] : 0;
            if (catIdx < 0) catIdx = 0;
            const float hue = (strUnique > 1)
                ? static_cast<float>(catIdx % strUnique) / static_cast<float>(strUnique)
                : 0.0f;
            // HSV(hue,1,1) → RGB via fast sector math
            const float h6 = hue * 5.9999f;
            const int   sec  = static_cast<int>(h6);
            const float frac = h6 - sec;
            switch (sec) {
                case 0:  r = 1.f;       g = frac;       b = 0.f; break;
                case 1:  r = 1.f-frac;  g = 1.f;        b = 0.f; break;
                case 2:  r = 0.f;       g = 1.f;        b = frac; break;
                case 3:  r = 0.f;       g = 1.f-frac;   b = 1.f; break;
                case 4:  r = frac;      g = 0.f;        b = 1.f; break;
                default: r = 1.f;       g = 0.f;        b = 1.f-frac; break;
            }
            val = static_cast<float>(catIdx);
        } else {
            // Numeric: gradient blue→green→red
            val = pattr ? pattr[i] : 0.0f;
            const float t  = std::clamp((val - m_minRange) * invRange, 0.0f, 1.0f);
            const float h6 = (1.0f - t) * 3.96f;
            const int   sec  = static_cast<int>(h6);
            const float frac = h6 - sec;
            switch (sec) {
                case 0:  r = 1.f;       g = frac;       b = 0.f; break;
                case 1:  r = 1.f-frac;  g = 1.f;        b = 0.f; break;
                case 2:  r = 0.f;       g = 1.f;        b = frac; break;
                default: r = 0.f;       g = 1.f-frac;   b = 1.f; break;
            }
        }

        e.color = QVector4D(r, g, b, 1.0f);
        e.instanceData = QVector4D(val, 0.f, 0.f, 0.f);

        m_instanceToModelIndex.push_back(static_cast<int>(i));
        ++addedCount;
    }

    *instanceCount = addedCount;
    instanceData.resize(static_cast<qsizetype>(addedCount * sizeof(InstanceTableEntry)));
    qDebug() << "[RENDER] getInstanceBuffer END —" << addedCount << "/" << count
             << "in" << std::chrono::duration_cast<Ms>(Clock::now() - t0).count() << "ms"
             << "| instanceData:" << (addedCount * sizeof(InstanceTableEntry)) / (1024*1024) << "MB"
             << "| process:" << ModelDiagnostics::processMemoryMB() << "MB";
    return instanceData;
}

QVariantMap BlockModelProvider::getBlockInfo(int instanceIndex) const {
    std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
    QVariantMap result;
    if (!m_model || instanceIndex < 0 || instanceIndex >= (int)m_instanceToModelIndex.size())
        return result;
    const int i = m_instanceToModelIndex[instanceIndex];

    // Spatial fields — always present, always fast
    result["_index"] = i;
    result["X"]      = m_model->x[i];
    result["Y"]      = m_model->y[i];
    result["Z"]      = m_model->z[i];
    result["X_span"] = (i < (int)m_model->x_span.size()) ? m_model->x_span[i] : 0.f;
    result["Y_span"] = (i < (int)m_model->y_span.size()) ? m_model->y_span[i] : 0.f;
    result["Z_span"] = (i < (int)m_model->z_span.size()) ? m_model->z_span[i] : 0.f;

    // Attribute columns — use pre-converted QString keys (no alloc per click)
    for (const auto& col : m_cachedColumns) {
        if (col.numeric) {
            if (i < (int)col.numeric->size())
                result[col.qname] = (*col.numeric)[i];
        } else if (col.interned) {
            // interned.get() returns a const string& from the pool — one array lookup
            result[col.qname] = QString::fromStdString(col.interned->get(i));
        }
    }
    return result;
}

QColor BlockModelProvider::mapValueToColor(float value) const {
    // Note: this method is not protected by the mutex but it is only called internally.
    // However, it's safer to just remove it or lock it if used from outside.
    // In this codebase it is not currently called.
    float range = m_maxRange - m_minRange;
    if (std::abs(range) < 0.0001f) return Qt::red;
    float t = std::clamp((value - m_minRange) / range, 0.0f, 1.0f);
    return QColor::fromHsvF((1.0f - t) * 0.66f, 1.0, 1.0);
}

} // namespace Mining
