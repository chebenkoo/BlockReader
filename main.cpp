#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QDebug>
#include <fstream>
#include <QRegularExpression>

#include "BlockModel/Reader.h"
#include "BlockModel/MicromineReader.h"
#include "BlockModel/SubprocessReader.h"
#include "BlockModel/SpatialIndex.h"
#include "BlockModel/Instancing.h"

using namespace Qt::StringLiterals;
using namespace Mining;

class ModelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList availableFields READ availableFields NOTIFY availableFieldsChanged)
    Q_PROPERTY(double modelRadius READ modelRadius NOTIFY boundsChanged)

public:
    explicit ModelController(QObject *parent = nullptr) : QObject(parent) {}

    QStringList availableFields() const { return m_availableFields; }
    double modelRadius() const { return m_modelRadius; }

    Q_INVOKABLE void preScan(const QUrl &fileUrl) {
        QString path = fileUrl.toLocalFile();
        if (path.isEmpty()) path = fileUrl.toString();
        m_lastPath = path;
        try {
            m_availableFields.clear();
            if (path.endsWith(".dat", Qt::CaseInsensitive)) {
                m_availableFields = SubprocessReader::getFields(path);
            } else {
                std::ifstream f(path.toStdString());
                std::string headerLine;
                if (std::getline(f, headerLine)) {
                    // Use a more robust split that handles quotes correctly
                    QString qHeader = QString::fromStdString(headerLine);
                    // Remove quotes from the entire header line before splitting
                    qHeader.replace("\"", "");
                    m_availableFields = qHeader.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
                }
            }
            emit availableFieldsChanged();
            m_status = "File scanned. " + QString::number(m_availableFields.size()) + " fields found.";
            emit statusChanged();
        } catch (const std::exception &e) {
            m_status = QString("Scan Error: %1").arg(e.what());
            emit statusChanged();
        }
    }

    Q_INVOKABLE void loadWithMapping(const QVariantMap &mapping) {
        m_status = "Loading...";
        emit statusChanged();

        try {
            std::map<std::string, std::string> stdMapping;
            for (auto it = mapping.begin(); it != mapping.end(); ++it) {
                stdMapping[it.key().toStdString()] = it.value().toString().toStdString();
            }

            if (m_lastPath.endsWith(".dat", Qt::CaseInsensitive)) {
                m_model = SubprocessReader::load(m_lastPath, stdMapping);
            } else {
                ColumnMapping csvMap;
                csvMap.x_col = stdMapping["X"];
                csvMap.y_col = stdMapping["Y"];
                csvMap.z_col = stdMapping["Z"];
                csvMap.x_span_col = stdMapping.count("XSPAN") ? stdMapping["XSPAN"] : "";
                csvMap.y_span_col = stdMapping.count("YSPAN") ? stdMapping["YSPAN"] : "";
                csvMap.z_span_col = stdMapping.count("ZSPAN") ? stdMapping["ZSPAN"] : "";
                
                csvMap.attribute_map.clear();
                for(auto const& [k, v] : stdMapping) {
                    // Skip spatial keys, everything else is an attribute
                    if (k != "X" && k != "Y" && k != "Z" && k != "XSPAN" && k != "YSPAN" && k != "ZSPAN") {
                        csvMap.attribute_map[k] = v;
                    }
                }
                m_model = Reader::load_from_csv(m_lastPath.toStdString(), csvMap);
            }
            
            qDebug() << "Load finished. Size:" << m_model.size();

            MicromineReader::center_model(m_model);
            
            // Calculate and log the centroid for verification
            double sx=0, sy=0, sz=0; size_t count=0;
            for(size_t i=0; i<m_model.size(); ++i) { sx+=m_model.x[i]; sy+=m_model.y[i]; sz+=m_model.z[i]; count++; }
            qDebug() << "CENTROID (relative to model space):" << (count > 0 ? sx/count : 0) << "," << (count > 0 ? sy/count : 0);

            calculateBounds();

            m_index.build(m_model);
            if (m_provider) {
                m_provider->setModel(&m_model);
                
                m_availableFields.clear();
                for (auto const& [name, _] : m_model.attributes) {
                    m_availableFields << QString::fromStdString(name);
                }
                emit availableFieldsChanged();

                if (!m_availableFields.isEmpty()) {
                    m_provider->setColorAttribute(m_availableFields.first());
                }
            }
            m_status = QString("Loaded %1 blocks").arg(m_model.size());
            emit statusChanged();
        } catch (const std::exception &e) {
            m_status = QString("Load Error: %1").arg(e.what());
            emit statusChanged();
        }
    }

    void calculateBounds() {
        if (m_model.empty()) return;
        double minX = 1e30, maxX = -1e30;
        double minY = 1e30, maxY = -1e30;
        double minZ = 1e30, maxZ = -1e30;
        bool foundAny = false;

        for (size_t i = 0; i < m_model.size(); ++i) {
            if (std::isfinite(m_model.x[i]) && std::isfinite(m_model.y[i]) && std::isfinite(m_model.z[i])) {
                minX = std::min(minX, m_model.x[i]); maxX = std::max(maxX, m_model.x[i]);
                minY = std::min(minY, m_model.y[i]); maxY = std::max(maxY, m_model.y[i]);
                minZ = std::min(minZ, m_model.z[i]); maxZ = std::max(maxZ, m_model.z[i]);
                foundAny = true;
            }
        }
        
        if (!foundAny) { m_modelRadius = 100; return; }

        double dx = maxX - minX;
        double dy = maxY - minY;
        double dz = maxZ - minZ;
        m_modelRadius = std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0;
        if (!std::isfinite(m_modelRadius) || m_modelRadius < 1.0) m_modelRadius = 100;
        
        qDebug() << "Model Bounds: min(" << minX << minY << minZ << ") max(" << maxX << maxY << maxZ << ")";
        qDebug() << "Calculated Model Radius:" << m_modelRadius;

        emit boundsChanged();
    }

    Q_INVOKABLE void loadModel(const QUrl &fileUrl) { preScan(fileUrl); }
    void setProvider(BlockModelProvider* p) { m_provider = p; }
    QString status() const { return m_status; }

signals:
    void statusChanged();
    void availableFieldsChanged();
    void boundsChanged();

private:
    BlockModelSoA m_model;
    SpatialIndex m_index;
    BlockModelProvider* m_provider = nullptr;
    QString m_status = "Ready";
    QStringList m_availableFields;
    QString m_lastPath;
    double m_modelRadius = 1000;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    qmlRegisterType<BlockModelProvider>("Mining", 1, 0, "BlockModelProvider");
    ModelController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("modelController", &controller);
    const QUrl url(u"qrc:/qt/qml/MiningSchedule/Main.qml"_s);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url, &controller](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) QCoreApplication::exit(-1);
        auto* provider = obj->findChild<BlockModelProvider*>("blockProvider");
        if (provider) controller.setProvider(provider);
    }, Qt::QueuedConnection);
    engine.load(url);
    return app.exec();
}

#include "main.moc"
