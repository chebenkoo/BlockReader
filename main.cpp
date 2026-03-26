#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QDebug>
#include <fstream>
#include <QRegularExpression>

#include "BlockModel/Reader.h"
#include "BlockModel/MicromineReader.h"
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
            if (path.endsWith(".dat", Qt::CaseInsensitive)) {
                MicromineReader::MetaData meta;
                auto vars = MicromineReader::get_variables(path.toStdString(), meta);
                m_availableFields.clear();
                for (const auto& v : vars) m_availableFields << QString::fromStdString(v.name);
            } else {
                std::ifstream f(path.toStdString());
                std::string header;
                std::getline(f, header);
                m_availableFields = QString::fromStdString(header).split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
            }
            emit availableFieldsChanged();
            m_status = "File scanned. Please map fields.";
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

            m_model = MicromineReader::load(m_lastPath.toStdString(), stdMapping);
            qDebug() << "Checkpoint: Load finished. Size:" << m_model.size();

            MicromineReader::center_model(m_model);
            qDebug() << "Checkpoint: Centering finished.";

            calculateBounds();
            qDebug() << "Checkpoint: Bounds calculated.";

            m_index.build(m_model);
            qDebug() << "Checkpoint: Spatial index built.";

            if (m_provider) {
                m_provider->setModel(&m_model);
                qDebug() << "Checkpoint: Provider model set.";
                for (auto const& [name, vec] : m_model.attributes) {
                    m_provider->setColorAttribute(QString::fromStdString(name));
                    break; 
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
