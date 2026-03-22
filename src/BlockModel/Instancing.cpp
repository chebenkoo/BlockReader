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
    if (!m_model) return;
    auto it = m_model->attributes.find(m_colorAttribute.toStdString());
    if (it != m_model->attributes.end() && !it->second.empty()) {
        float min = *std::min_element(it->second.begin(), it->second.end());
        float max = *std::max_element(it->second.begin(), it->second.end());
        m_minRange = min;
        m_maxRange = max;
        std::cout << "Auto-Range for " << m_colorAttribute.toStdString() << ": [" << min << ", " << max << "]" << std::endl;
        emit rangeChanged();
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
void BlockModelProvider::setBlockSize(float val) { m_blockSize = val; emit blockSizeChanged(); markDirty(); }

QByteArray BlockModelProvider::getInstanceBuffer(int *instanceCount) {
    if (!m_model || m_model->empty()) { *instanceCount = 0; return QByteArray(); }

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
        if (!std::isfinite(m_model->x[i]) || !std::isfinite(m_model->y[i])) continue;
        if (!m_model->visible.empty() && m_model->visible[i] == 0) continue;
        
        float gradeVal = (gradeValues && i < gradeValues->size()) ? (*gradeValues)[i] : 0.0f;
        if (gradeVal < m_minGrade) continue;

        // --- AXIS REMAP (Mining -> Qt Quick 3D) ---
        // Mining: X=East, Y=North, Z=Elev
        // Qt:     X=Right, Y=Up, Z=Backward (we use -Y for Forward)
        entry[addedCount].position = QVector3D(
            static_cast<float>(m_model->x[i]),      // East -> X
            static_cast<float>(m_model->z[i]),      // Elev -> Y (UP)
            static_cast<float>(-m_model->y[i])      // North -> -Z (Depth)
        );

        // --- SCALE REMAP ---
        float sx = (m_model->x_span.size() > i) ? m_model->x_span[i] * 2.0f : 10.0f;
        float sy = (m_model->z_span.size() > i) ? m_model->z_span[i] * 2.0f : 5.0f; // Z -> Y
        float sz = (m_model->y_span.size() > i) ? m_model->y_span[i] * 2.0f : 10.0f; // Y -> Z

        entry[addedCount].scale = QVector3D(sx * m_blockSize, sy * m_blockSize, sz * m_blockSize);

        // --- COLOR ---
        float value = (attrValues && i < attrValues->size()) ? (*attrValues)[i] : 0.0f;
        QColor color = mapValueToColor(value);
        entry[addedCount].color = QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
        
        entry[addedCount].eulerRotation = QVector3D(0, 0, 0);
        entry[addedCount].customData = QVector4D(value, 0, 0, 0);
        addedCount++;
    }

    *instanceCount = addedCount;
    instanceData.resize(addedCount * sizeof(VoxelInstanceEntry));
    return instanceData;
}

QColor BlockModelProvider::mapValueToColor(float value) const {
    if (m_maxRange <= m_minRange) return Qt::white;
    float t = std::clamp((value - m_minRange) / (m_maxRange - m_minRange), 0.0f, 1.0f);
    return QColor::fromHsvF((1.0f - t) * 0.66f, 1.0, 1.0);
}

} // namespace Mining
