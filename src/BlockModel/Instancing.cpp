#include "BlockModel/Instancing.h"
#include <QtGui/qvector3d.h>
#include <QtGui/qvector4d.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Mining {


BlockModelProvider::BlockModelProvider(QQuick3DObject *parent) : QQuick3DInstancing(parent) {}
BlockModelProvider::~BlockModelProvider() = default;

void BlockModelProvider::autoComputeRange() {
    if (!m_model || m_model->attributes.empty()) return;
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
        std::cout << "BlockModelProvider: No model or empty!" << std::endl;
        return QByteArray(); 
    }

    const size_t count = m_model->size();
    std::cout << "[Instancing] Building buffer: " << count << " blocks total\n";
    std::cout << "[Instancing] colorAttribute='" << m_colorAttribute.toStdString()
              << "' blockSize=" << m_blockSize
              << " gridMode=" << m_gridMode
              << " minGrade=" << m_minGrade << "\n";
    std::cout << "[Instancing] Model x span count=" << m_model->x_span.size()
              << " y_span=" << m_model->y_span.size()
              << " z_span=" << m_model->z_span.size() << "\n";
    std::cout << "[Instancing] Attributes loaded:";
    for (auto const& [k, v] : m_model->attributes)
        std::cout << " '" << k << "'(" << v.size() << ")";
    std::cout << "\n";

    QByteArray instanceData;
    instanceData.resize(count * sizeof(InstanceTableEntry));
    auto* entry = reinterpret_cast<InstanceTableEntry*>(instanceData.data());

    const std::vector<float>* attrValues = nullptr;
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end()) {
        attrValues = &it->second;
        std::cout << "[Instancing] Color attribute found, size=" << attrValues->size() << "\n";
    } else {
        std::cout << "[Instancing] WARNING: color attribute '" << m_colorAttribute.toStdString() << "' not found\n";
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

    // Derive fallback span from first valid block in the data, not a magic number
    float fallback_sx = 10.0f, fallback_sy = 5.0f, fallback_sz = 10.0f;
    for (size_t fi = 0; fi < m_model->x_span.size(); ++fi) {
        if (m_model->x_span[fi] > 0.01f) {
            fallback_sx = m_model->x_span[fi];
            fallback_sy = (m_model->z_span.size() > fi) ? m_model->z_span[fi] : fallback_sx;
            fallback_sz = (m_model->y_span.size() > fi) ? m_model->y_span[fi] : fallback_sx;
            break;
        }
    }
    std::cout << "[Instancing] Fallback span: sx=" << fallback_sx
              << " sy=" << fallback_sy << " sz=" << fallback_sz << "\n";

    int skippedNonFinite = 0, skippedInvisible = 0, skippedGrade = 0;
    int addedCount = 0;
    float xMin=1e30f, xMax=-1e30f, yMin=1e30f, yMax=-1e30f, zMin=1e30f, zMax=-1e30f;
    float sxSample=0, sySample=0, szSample=0;

    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(m_model->x[i]) || !std::isfinite(m_model->y[i]) || !std::isfinite(m_model->z[i])) { skippedNonFinite++; continue; }
        if (!m_model->visible.empty() && m_model->visible[i] == 0) { skippedInvisible++; continue; }

        float gradeVal = (gradeValues && i < gradeValues->size()) ? (*gradeValues)[i] : 0.0f;
        if (std::isfinite(gradeVal) && gradeVal < m_minGrade) { skippedGrade++; continue; }

        // Position: Z-up to Y-up mapping
        QVector3D position(m_model->x[i], m_model->z[i], -m_model->y[i]);

        // Track bounds of first 5 blocks for inspection
        if (addedCount < 5) {
            std::cout << "[Instancing] Block[" << addedCount << "] raw xyz=("
                      << m_model->x[i] << "," << m_model->y[i] << "," << m_model->z[i]
                      << ") spans=(" << m_model->x_span[i] << "," << m_model->y_span[i] << "," << m_model->z_span[i] << ")"
                      << " -> pos=(" << position.x() << "," << position.y() << "," << position.z() << ")\n";
        }

        xMin=std::min(xMin,position.x()); xMax=std::max(xMax,position.x());
        yMin=std::min(yMin,position.y()); yMax=std::max(yMax,position.y());
        zMin=std::min(zMin,position.z()); zMax=std::max(zMax,position.z());

        // --- GRID vs VOXEL SCALE ---
        float sx = (m_model->x_span.size() > i && m_model->x_span[i] > 0.01f) ? m_model->x_span[i] : fallback_sx;
        float sy = (m_model->z_span.size() > i && m_model->z_span[i] > 0.01f) ? m_model->z_span[i] : fallback_sy;
        float sz = (m_model->y_span.size() > i && m_model->y_span[i] > 0.01f) ? m_model->y_span[i] : fallback_sz;
        if (addedCount == 0) { sxSample=sx; sySample=sy; szSample=sz; }

        // In Grid Mode, we shrink blocks to 95% to see the boundaries
        float visualScale = m_gridMode ? 0.95f : 1.0f;
        // NOTE: Standard Qt Quick 3D #Cube is 100x100x100.
        // To map 1 meter to 1 unit, we must divide our spans by 100.
        QVector3D scale(
            (sx / 100.0f) * m_blockSize * visualScale,
            (sy / 100.0f) * m_blockSize * visualScale,
            (sz / 100.0f) * m_blockSize * visualScale
        );

        float val = (attrValues && i < attrValues->size()) ? (*attrValues)[i] : 0.0f;
        QColor color = mapValueToColor(val);

        entry[addedCount] = calculateTableEntry(
            position,
            scale,
            QVector3D(m_rotX, m_rotY, m_rotZ),
            color,
            QVector4D(val, 0, 0, 0)
        );
        m_instanceToModelIndex.push_back((int)i);
        addedCount++;
    }

    std::cout << "[Instancing] Done: added=" << addedCount
              << " skipped(nonFinite=" << skippedNonFinite
              << " invisible=" << skippedInvisible
              << " grade=" << skippedGrade << ")\n";
    std::cout << "[Instancing] Scene bounds X[" << xMin << "," << xMax
              << "] Y[" << yMin << "," << yMax
              << "] Z[" << zMin << "," << zMax << "]\n";
    std::cout << "[Instancing] Sample span(raw): sx=" << sxSample << " sy=" << sySample << " sz=" << szSample
              << " -> scale=(" << (sxSample/100.f)*m_blockSize << ","
              << (sySample/100.f)*m_blockSize << ","
              << (szSample/100.f)*m_blockSize << ")\n";
    std::cout << std::flush;

    *instanceCount = addedCount;
    instanceData.resize(addedCount * sizeof(InstanceTableEntry));
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
