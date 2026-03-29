#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QDebug>
#include <fstream>
#include <chrono>
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
    QString status() const { return m_status; }

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
                    QString qHeader = QString::fromStdString(headerLine);
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
        using Clock = std::chrono::steady_clock;
        using Ms    = std::chrono::milliseconds;
        auto elapsed = [](Clock::time_point t0) {
            return std::chrono::duration_cast<Ms>(Clock::now() - t0).count();
        };

        m_status = "Loading...";
        emit statusChanged();

        std::map<std::string, std::string> stdMapping;
        for (auto it = mapping.begin(); it != mapping.end(); ++it)
            stdMapping[it.key().toStdString()] = it.value().toString().toStdString();

        const auto t_total = Clock::now();

        try {
            // ── Step 1: Read file ──────────────────────────────────────────────
            {
                auto t = Clock::now();
                qDebug() << "[LOAD] Step 1: reading file...";
                if (m_lastPath.endsWith(".dat", Qt::CaseInsensitive)) {
                    m_model = SubprocessReader::load(m_lastPath, stdMapping);
                } else {
                    ColumnMapping csvMap;
                    csvMap.x_col = stdMapping.at("X");
                    csvMap.y_col = stdMapping.at("Y");
                    csvMap.z_col = stdMapping.at("Z");
                    csvMap.x_span_col = stdMapping.count("XSPAN") ? stdMapping.at("XSPAN") : "";
                    csvMap.y_span_col = stdMapping.count("YSPAN") ? stdMapping.at("YSPAN") : "";
                    csvMap.z_span_col = stdMapping.count("ZSPAN") ? stdMapping.at("ZSPAN") : "";
                    for (auto const& [k, v] : stdMapping) {
                        if (k != "X" && k != "Y" && k != "Z" &&
                            k != "XSPAN" && k != "YSPAN" && k != "ZSPAN")
                            csvMap.attribute_map[k] = v;
                    }
                    m_model = Reader::load_from_csv(m_lastPath.toStdString(), csvMap);
                }
                qDebug() << "[LOAD] Step 1 done:" << elapsed(t) << "ms —"
                         << m_model.size() << "blocks,"
                         << m_model.attributes.size() << "numeric attrs,"
                         << m_model.string_attributes.size() << "string attrs";
            }

            // ── Step 2: Centre + Morton sort ───────────────────────────────────
            {
                auto t = Clock::now();
                MicromineReader::center_model(m_model);
                qDebug() << "[LOAD] center_model:" << elapsed(t) << "ms";

                auto t2 = Clock::now();
                m_model.sort_by_morton();
                qDebug() << "[LOAD] sort_by_morton:" << elapsed(t2) << "ms";
            }

            // ── Step 3: Bounding radius ────────────────────────────────────────
            {
                auto t = Clock::now();
                float minX=1e30f, maxX=-1e30f;
                float minY=1e30f, maxY=-1e30f;
                float minZ=1e30f, maxZ=-1e30f;
                bool found = false;
                for (size_t i = 0; i < m_model.size(); ++i) {
                    if (std::isfinite(m_model.x[i]) && std::isfinite(m_model.y[i]) && std::isfinite(m_model.z[i])) {
                        minX=std::min(minX,m_model.x[i]); maxX=std::max(maxX,m_model.x[i]);
                        minY=std::min(minY,m_model.y[i]); maxY=std::max(maxY,m_model.y[i]);
                        minZ=std::min(minZ,m_model.z[i]); maxZ=std::max(maxZ,m_model.z[i]);
                        found = true;
                    }
                }
                if (found) {
                    double dx=maxX-minX, dy=maxY-minY, dz=maxZ-minZ;
                    m_modelRadius = std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0;
                    if (!std::isfinite(m_modelRadius) || m_modelRadius < 1.0) m_modelRadius = 100.0;
                }
                qDebug() << "[LOAD] bounds:" << elapsed(t) << "ms — radius:" << m_modelRadius;
            }

            // ── Step 4: Field list ─────────────────────────────────────────────
            m_availableFields.clear();
            for (auto const& [name, _] : m_model.attributes)
                m_availableFields << QString::fromStdString(name);
            for (auto const& [name, _] : m_model.string_attributes)
                m_availableFields << QString::fromStdString(name);
            qDebug() << "[LOAD] fields:" << m_availableFields;

            // ── Step 5: Hand model to renderer ────────────────────────────────
            qDebug() << "[LOAD] calling setModel...";
            if (m_provider) {
                m_provider->setModel(&m_model);
                qDebug() << "[LOAD] setModel done";

                qDebug() << "[LOAD] emitting availableFieldsChanged...";
                emit availableFieldsChanged();
                qDebug() << "[LOAD] availableFieldsChanged done";

                if (!m_availableFields.isEmpty()) {
                    qDebug() << "[LOAD] calling setColorAttribute:" << m_availableFields.first();
                    m_provider->setColorAttribute(m_availableFields.first());
                    qDebug() << "[LOAD] setColorAttribute done";
                }
            }

            qDebug() << "[LOAD] total:" << elapsed(t_total) << "ms";
            m_status = QString("Loaded %1 blocks").arg(m_model.size());
            emit boundsChanged();
            emit statusChanged();

        } catch (const std::exception &e) {
            qDebug() << "[LOAD] EXCEPTION:" << e.what();
            m_status = QString("Load Error: %1").arg(e.what());
            emit statusChanged();
        } catch (...) {
            qDebug() << "[LOAD] UNKNOWN EXCEPTION";
            m_status = "Load Error: unknown";
            emit statusChanged();
        }
    }

    Q_INVOKABLE void loadModel(const QUrl &fileUrl) { preScan(fileUrl); }
    void setProvider(BlockModelProvider* p) { m_provider = p; }

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
