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
    std::cout << "BlockModelProvider: Building buffer for " << count << " blocks..." << std::endl;
    QByteArray instanceData;
    instanceData.resize(count * sizeof(VoxelInstanceEntry));
    auto* entry = reinterpret_cast<VoxelInstanceEntry*>(instanceData.data());

    const std::vector<float>* attrValues = nullptr;
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end()) attrValues = &it->second;

    const std::vector<float>* gradeValues = nullptr;
    auto git = m_model->attributes.find("Grade");
    if (git != m_model->attributes.end()) {
        gradeValues = &git->second;
    } else {
        // Fallback: use the currently colored attribute for the cutoff slider
        gradeValues = attrValues;
    }

    int addedCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(m_model->x[i]) || !std::isfinite(m_model->y[i]) || !std::isfinite(m_model->z[i])) continue;
        if (!m_model->visible.empty() && m_model->visible[i] == 0) continue;
        
        float gradeVal = (gradeValues && i < gradeValues->size()) ? (*gradeValues)[i] : 0.0f;
        
        // Only apply cutoff if the attribute exists and is finite
        if (std::isfinite(gradeVal) && gradeVal < m_minGrade) continue;

        // Position: Z-up to Y-up mapping
        entry[addedCount].position = QVector3D((float)m_model->x[i], (float)m_model->z[i], (float)-m_model->y[i]);

        // --- GRID vs VOXEL SCALE ---
        float sx = (m_model->x_span.size() > i && m_model->x_span[i] > 0.01f) ? m_model->x_span[i] : 10.0f;
        float sy = (m_model->z_span.size() > i && m_model->z_span[i] > 0.01f) ? m_model->z_span[i] : 5.0f;
        float sz = (m_model->y_span.size() > i && m_model->y_span[i] > 0.01f) ? m_model->y_span[i] : 10.0f;

        // In Grid Mode, we shrink blocks to 95% to see the boundaries
        float visualScale = m_gridMode ? 0.95f : 1.0f;
        // NOTE: Standard Qt Quick 3D #Cube is 100x100x100. 
        // To map 1 meter to 1 unit, we must divide our spans by 100.
        entry[addedCount].scale = QVector3D(
            (sx / 100.0f) * m_blockSize * visualScale, 
            (sy / 100.0f) * m_blockSize * visualScale, 
            (sz / 100.0f) * m_blockSize * visualScale
        );
        
        entry[addedCount].eulerRotation = QVector3D(m_rotX, m_rotY, m_rotZ);

        float val = (attrValues && i < attrValues->size()) ? (*attrValues)[i] : 0.0f;
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
