#pragma once

#include <QtQuick3D/qquick3dinstancing.h>
#include <QtQuick3D/qquick3dobject.h>
#include <QtCore/qstring.h>
#include <QtCore/qvariant.h>
#include <QtGui/qvector3d.h>
#include <QtGui/qcolor.h>
#include <vector>
#include "BlockModel.h"

namespace Mining {

class BlockModelProvider : public QQuick3DInstancing {
    Q_OBJECT
    Q_PROPERTY(QString colorAttribute READ colorAttribute WRITE setColorAttribute NOTIFY colorAttributeChanged)
    Q_PROPERTY(float minRange READ minRange WRITE setMinRange NOTIFY rangeChanged)
    Q_PROPERTY(float maxRange READ maxRange WRITE setMaxRange NOTIFY rangeChanged)
    Q_PROPERTY(float gradeMax READ gradeMax NOTIFY gradeMaxChanged)
    Q_PROPERTY(float minGrade READ minGrade WRITE setMinGrade NOTIFY minGradeChanged)
    Q_PROPERTY(float blockSize READ blockSize WRITE setBlockSize NOTIFY blockSizeChanged)
    Q_PROPERTY(bool gridMode READ gridMode WRITE setGridMode NOTIFY gridModeChanged)

public:
    explicit BlockModelProvider(QQuick3DObject *parent = nullptr);
    ~BlockModelProvider();

    void setModel(const BlockModelSoA* model);
    Q_INVOKABLE void setModelRotation(float rx, float ry, float rz);
    Q_INVOKABLE QVariantMap getBlockInfo(int instanceIndex) const;

    QByteArray getInstanceBuffer(int *instanceCount) override;

    QString colorAttribute() const { return m_colorAttribute; }
    void setColorAttribute(const QString &attr);

    float minRange() const { return m_minRange; }
    void setMinRange(float val);

    float maxRange() const { return m_maxRange; }
    void setMaxRange(float val);

    float minGrade() const { return m_minGrade; }
    void setMinGrade(float val);

    float blockSize() const { return m_blockSize; }
    void setBlockSize(float val);

    bool gridMode() const { return m_gridMode; }
    void setGridMode(bool val);

    float gradeMax() const { return m_gradeMax; }

signals:
    void colorAttributeChanged();
    void rangeChanged();
    void gradeMaxChanged();
    void minGradeChanged();
    void blockSizeChanged();
    void gridModeChanged();

private:
    void autoComputeRange();
    const BlockModelSoA* m_model = nullptr;
    std::vector<int> m_instanceToModelIndex;
    QString m_colorAttribute = "Grade";
    float m_minRange = 0.0f;
    float m_maxRange = 1.0f;
    float m_minGrade = 0.0f;
    float m_gradeMax = 1.0f;
    float m_blockSize = 1.0f;
    bool m_gridMode = true;
    float m_rotX = 0, m_rotY = 0, m_rotZ = 0;
    
    QColor mapValueToColor(float value) const;
};

} // namespace Mining
