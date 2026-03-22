#include "BlockModel/Instancing.h"
#include <QtGui/qvector3d.h>
#include <QtGui/qvector4d.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Mining {

struct VoxelInstanceEntry {
    QVector3D position;
    QVector3D scale;
    QVector3D eulerRotation;
    QVector4D color;
    QVector4D customData;
};

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
            if (std::isfinite(v)) {
                min = std::min(min, v);
                max = std::max(max, v);
                found = true;
            }
        }
        if (found) {
            m_minRange = min;
            m_maxRange = (max > min) ? max : min + 1.0f;
            std::cout << "GPU DEBUG: Color Range [" << m_minRange << " to " << m_maxRange << "] for " << m_colorAttribute.toStdString() << std::endl;
            emit rangeChanged();
        }
    }
}

void BlockModelProvider::setModel(const BlockModelSoA* model) {
    m_model = model;
    autoComputeRange();
    markDirty(); 
}

void BlockModelProvider::setColorAttribute(const QString &attr) {
    if (m_colorAttribute != attr) {
        m_colorAttribute = attr;
        autoComputeRange();
        emit colorAttributeChanged();
        markDirty();
    }
}

void BlockModelProvider::setMinRange(float val) { m_minRange = val; emit rangeChanged(); markDirty(); }
void BlockModelProvider::setMaxRange(float val) { m_maxRange = val; emit rangeChanged(); markDirty(); }
void BlockModelProvider::setMinGrade(float val) { m_minGrade = val; emit minGradeChanged(); markDirty(); }
void BlockModelProvider::setBlockSize(float val) { if (m_blockSize != val) { m_blockSize = val; emit blockSizeChanged(); markDirty(); } }

void BlockModelProvider::setModelRotation(float rx, float ry, float rz) {
    m_rotX = rx; m_rotY = ry; m_rotZ = rz;
    markDirty();
}

QByteArray BlockModelProvider::getInstanceBuffer(int *instanceCount) {
    if (!m_model || m_model->empty()) {
        *instanceCount = 0;
        return QByteArray();
    }

    const size_t count = m_model->size();
    QByteArray instanceData;
    instanceData.resize(count * sizeof(VoxelInstanceEntry));
    auto* entry = reinterpret_cast<VoxelInstanceEntry*>(instanceData.data());

    const std::vector<float>* attrValues = nullptr;
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end()) attrValues = &it->second;

    const std::vector<float>* gradeValues = nullptr;
    auto git = m_model->attributes.find("Grade");
    if (git != m_model->attributes.end()) gradeValues = &git->second;

    int addedCount = 0;
    for (size_t i = 0; i < count; ++i) {
        // --- CRITICAL FINITE CHECK ---
        if (!std::isfinite(m_model->x[i]) || !std::isfinite(m_model->y[i]) || !std::isfinite(m_model->z[i])) continue;
        if (!m_model->visible.empty() && m_model->visible[i] == 0) continue;
        
        float gradeVal = (gradeValues && i < gradeValues->size()) ? (*gradeValues)[i] : 0.0f;
        if (std::isfinite(gradeVal) && gradeVal < m_minGrade) continue;

        // Position Remap (Z-up to Y-up)
        entry[addedCount].position = QVector3D(
            static_cast<float>(m_model->x[i]),
            static_cast<float>(m_model->z[i]),
            static_cast<float>(-m_model->y[i])
        );

        // Scale Logic
        float sx = (m_model->x_span.size() > i && std::isfinite(m_model->x_span[i])) ? m_model->x_span[i] * 2.0f : 10.0f;
        float sy = (m_model->z_span.size() > i && std::isfinite(m_model->z_span[i])) ? m_model->z_span[i] * 2.0f : 5.0f;
        float sz = (m_model->y_span.size() > i && std::isfinite(m_model->y_span[i])) ? m_model->y_span[i] * 2.0f : 10.0f;

        entry[addedCount].scale = QVector3D(sx * m_blockSize, sy * m_blockSize, sz * m_blockSize);
        entry[addedCount].eulerRotation = QVector3D(m_rotX, m_rotY, m_rotZ);

        float val = (attrValues && i < attrValues->size()) ? (*attrValues)[i] : 0.0f;
        if (!std::isfinite(val)) val = m_minRange; // Default to min of range if invalid
        
        QColor color = mapValueToColor(val);
        entry[addedCount].color = QVector4D(color.redF(), color.greenF(), color.blueF(), 1.0f);
        entry[addedCount].customData = QVector4D(val, 0, 0, 0);

        addedCount++;
    }

    *instanceCount = addedCount;
    instanceData.resize(addedCount * sizeof(VoxelInstanceEntry));
    return instanceData;
}

QColor BlockModelProvider::mapValueToColor(float value) const {
    float range = m_maxRange - m_minRange;
    if (std::abs(range) < 0.0001f) return Qt::red;
    float t = std::clamp((value - m_minRange) / range, 0.0f, 1.0f);
    return QColor::fromHsvF((1.0f - t) * 0.66f, 1.0, 1.0);
}

} // namespace Mining
