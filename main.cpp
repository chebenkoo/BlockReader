#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QDebug>
#include <fstream>
#include <chrono>
#include <QRegularExpression>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>

#include "BlockModel/Reader.h"
#include "BlockModel/MicromineReader.h"
#include "BlockModel/SubprocessReader.h"
#include "BlockModel/SpatialIndex.h"
#include "BlockModel/Instancing.h"
#include "BlockModel/ModelDiagnostics.h"
#include "BlockModel/FormulaEvaluator.h"
#include "BlockModel/AnalyticsEngine.h"

// Convenience alias so log sites stay readable
static size_t procMB() { return Mining::ModelDiagnostics::processMemoryMB(); }

using namespace Qt::StringLiterals;
using namespace Mining;

// Pure function — safe to call on any thread, touches no shared state.
static double calculateBounds(const BlockModelSoA& model)
{
    float minX=1e30f, maxX=-1e30f;
    float minY=1e30f, maxY=-1e30f;
    float minZ=1e30f, maxZ=-1e30f;
    bool found = false;
    for (size_t i = 0; i < model.size(); ++i) {
        if (std::isfinite(model.x[i]) && std::isfinite(model.y[i]) && std::isfinite(model.z[i])) {
            minX=std::min(minX,model.x[i]); maxX=std::max(maxX,model.x[i]);
            minY=std::min(minY,model.y[i]); maxY=std::max(maxY,model.y[i]);
            minZ=std::min(minZ,model.z[i]); maxZ=std::max(maxZ,model.z[i]);
            found = true;
        }
    }
    if (!found) return 100.0;
    double dx=maxX-minX, dy=maxY-minY, dz=maxZ-minZ;
    double r = std::sqrt(dx*dx + dy*dy + dz*dz) / 2.0;
    return (std::isfinite(r) && r >= 1.0) ? r : 100.0;
}

class ModelController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QStringList availableFields READ availableFields NOTIFY availableFieldsChanged)
    Q_PROPERTY(QStringList stringFields READ stringFields NOTIFY availableFieldsChanged)
    Q_PROPERTY(QStringList numericFields READ numericFields NOTIFY availableFieldsChanged)
    Q_PROPERTY(double modelRadius READ modelRadius NOTIFY boundsChanged)

public:
    explicit ModelController(QObject *parent = nullptr) : QObject(parent) {}

    QStringList availableFields() const { return m_availableFields; }
    
    QStringList stringFields() const { 
        QStringList res;
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        for (auto const& [name, _] : m_model.string_attributes) res << QString::fromStdString(name);
        return res;
    }

    QStringList numericFields() const { 
        QStringList res;
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        for (auto const& [name, _] : m_model.attributes) res << QString::fromStdString(name);
        return res;
    }

    Q_INVOKABLE QVariantList getSummary(const QString &groupField, const QString &gradeField, const QString &densityField = "", const QString &filterField = "", const QString &filterValue = "") {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        if (m_model.empty()) return {};

        auto results = AnalyticsEngine::computeSummary(
            m_model, 
            groupField.toStdString(), 
            gradeField.toStdString(), 
            densityField.toStdString(),
            filterField.toStdString(),
            filterValue.toStdString()
        );

        QVariantList list;
        for (const auto& row : results) {
            QVariantMap map;
            map["group"] = QString::fromStdString(row.groupName);
            map["count"] = (double)row.count;
            map["volume"] = row.volume;
            map["tonnes"] = row.tonnes;
            map["avgGrade"] = row.avgGrade;
            map["metal"] = row.metal;
            list << map;
        }
        return list;
    }

    Q_INVOKABLE QStringList getStringValues(const QString &fieldName) {
        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
        QStringList res;
        auto it = m_model.string_attributes.find(fieldName.toStdString());
        if (it != m_model.string_attributes.end()) {
            for (const auto& val : it->second.unique_values) {
                res << QString::fromStdString(val);
            }
        }
        return res;
    }

    double modelRadius() const { return m_modelRadius; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    bool isLoading() const { return m_isLoading; }

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

    Q_INVOKABLE void loadWithMapping(const QVariantMap &mapping, const QVariantList &formulas = {}) {
        if (m_isLoading) return;
        m_isLoading = true;
        emit isLoadingChanged();
        m_progress = 0.0;
        emit progressChanged();

        m_status = "Loading...";
        emit statusChanged();

        const QString path = m_lastPath;
        std::map<std::string, std::string> stdMapping;
        for (auto it = mapping.begin(); it != mapping.end(); ++it)
            stdMapping[it.key().toStdString()] = it.value().toString().toStdString();

        std::vector<FormulaEvaluator::Formula> stdFormulas;
        for (const QVariant& f : formulas) {
            QVariantMap fm = f.toMap();
            stdFormulas.push_back({
                fm["name"].toString().toStdString(),
                fm["expr"].toString().toStdString()
            });
        }

        m_loadFuture = QtConcurrent::run([this, path, stdMapping, stdFormulas]() {
            using Clock = std::chrono::steady_clock;
            using Ms    = std::chrono::milliseconds;
            auto elapsed = [](Clock::time_point t0) {
                return std::chrono::duration_cast<Ms>(Clock::now() - t0).count();
            };
            const auto t_total = Clock::now();

            try {
                // ── Step 1: Read (worker thread, no shared state touched) ───────
                BlockModelSoA localModel;
                qDebug() << "[MEM] Initial localModel — model:" << localModel.current_memory_usage() / (1024.0 * 1024.0)
                         << "MB | process:" << procMB() << "MB";
                {
                    auto t = Clock::now();
                    qDebug() << "[LOAD] Step 1: reading file...";

                    auto progressCb = [this](const Reader::Progress& p) {
                        double progressVal = 0.0;
                        if (p.total_rows > 0)
                            progressVal = (double)p.current_row / (double)p.total_rows;

                        QMetaObject::invokeMethod(this, [this, progressVal, msg = QString::fromStdString(p.message)]() {
                            // Scale Step 1 (Parsing) to 0.0 - 0.5 range
                            m_progress = progressVal * 0.5;
                            m_status = msg;
                            emit progressChanged();
                            emit statusChanged();
                        });
                    };

                    if (path.endsWith(".dat", Qt::CaseInsensitive)) {
                        localModel = SubprocessReader::load(path, stdMapping);
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
                        localModel = Reader::load_from_csv(path.toStdString(), csvMap, progressCb);
                    }
                    qDebug() << "[LOAD] Step 1 done:" << elapsed(t) << "ms —"
                             << localModel.size() << "blocks,"
                             << localModel.attributes.size() << "numeric attrs";
                }

                // ── Step 1.5: Calculate Formulas ─────────────────────────────
                if (!stdFormulas.empty()) {
                    QMetaObject::invokeMethod(this, [this]() {
                        m_status = "Calculating Formulas...";
                        m_progress = 0.55;
                        emit statusChanged();
                        emit progressChanged();
                    });
                    auto t = Clock::now();
                    std::string err = FormulaEvaluator::evaluate(localModel, stdFormulas);
                    if (!err.empty()) {
                        qDebug() << "[LOAD] Formula error:" << err.c_str();
                        // We continue loading but might want to notify user later.
                    }
                    qDebug() << "[LOAD] Formulas done:" << elapsed(t) << "ms";
                }

                // ── Step 2: Centre + Morton sort (worker thread) ─────────────
                {
                    QMetaObject::invokeMethod(this, [this]() {
                        m_status = "Centering Model...";
                        m_progress = 0.5; // Roughly half way through total process
                        emit statusChanged();
                        emit progressChanged();
                    });
                    auto t = Clock::now();
                    qDebug() << "[MEM] Pre center_model — process:" << procMB() << "MB";
                    MicromineReader::center_model(localModel);
                    qDebug() << "[LOAD] center_model:" << elapsed(t) << "ms";
                    qDebug() << "[MEM] Post center_model — process:" << procMB() << "MB";

                    QMetaObject::invokeMethod(this, [this]() {
                        m_status = "Sorting (Morton)...";
                        m_progress = 0.6;
                        emit statusChanged();
                        emit progressChanged();
                    });
                    auto t2 = Clock::now();
                    qDebug() << "[MEM] Pre sort_by_morton — model:" << localModel.current_memory_usage() / (1024.0 * 1024.0)
                             << "MB | process:" << procMB() << "MB";
                    localModel.sort_by_morton();
                    qDebug() << "[LOAD] sort_by_morton:" << elapsed(t2) << "ms";
                    qDebug() << "[MEM] Post sort_by_morton — model:" << localModel.current_memory_usage() / (1024.0 * 1024.0)
                             << "MB | process:" << procMB() << "MB";
                }

                // ── Step 3: Bounds + field list (worker thread) ──────────────
                const double radius = calculateBounds(localModel);
                qDebug() << "[LOAD] bounds — radius:" << radius;

                // Pre-calculate ranges for all numeric attributes on worker thread
                {
                    QMetaObject::invokeMethod(this, [this]() {
                        m_status = "Computing Attribute Ranges...";
                        m_progress = 0.8;
                        emit statusChanged();
                        emit progressChanged();
                    });
                    auto t = Clock::now();
                    for (auto& [name, vec] : localModel.attributes) {
                        float minV = 1e30f, maxV = -1e30f;
                        bool found = false;
                        for (float v : vec) {
                            if (std::isfinite(v)) {
                                minV = std::min(minV, v);
                                maxV = std::max(maxV, v);
                                found = true;
                            }
                        }
                        if (found) localModel.attribute_ranges[name] = {minV, maxV};
                    }
                    qDebug() << "[LOAD] compute ranges:" << elapsed(t) << "ms";
                }

                QStringList fields;
                for (auto const& [name, _] : localModel.attributes)
                    fields << QString::fromStdString(name);
                for (auto const& [name, _] : localModel.string_attributes)
                    fields << QString::fromStdString(name);

                qDebug() << "[LOAD] background done:" << elapsed(t_total) << "ms — posting to main thread. Process:" << procMB() << "MB";

                // Move model to heap and wrap in shared_ptr to guarantee zero copies during event capture.
                qDebug() << "[LOAD] Moving localModel to heap (shared_ptr)...";
                auto sharedModel = std::make_shared<BlockModelSoA>(std::move(localModel));
                qDebug() << "[LOAD] Model moved to shared_ptr. process:" << procMB() << "MB";

                // ── Step 4: Swap on main thread ──────────────────────────────
                qDebug() << "[LOAD] Requesting QMetaObject::invokeMethod...";
                bool postSuccess = QMetaObject::invokeMethod(this,
                    [this, sharedModel, fields, radius]() mutable
                {
                    qDebug() << "[MAIN] lambda started — process:" << procMB() << "MB";
                    using Clock2 = std::chrono::steady_clock;
                    using Ms2    = std::chrono::milliseconds;
                    const auto tm = Clock2::now();

                    {
                        qDebug() << "[MAIN] acquiring mutex for swap...";
                        std::lock_guard<std::mutex> lock(ModelDiagnostics::modelMutex());
                        qDebug() << "[MAIN] mutex acquired. Moving model from shared_ptr...";
                        m_model           = std::move(*sharedModel);
                        m_modelRadius     = radius;
                        m_availableFields = fields;
                        qDebug() << "[MAIN] model moved. Mutex releasing...";
                    } 

                    qDebug() << "[MAIN] swap done:"
                             << std::chrono::duration_cast<Ms2>(Clock2::now()-tm).count() << "ms";
                    qDebug() << "[MEM] After mutex swap — model:" << m_model.current_memory_usage() / (1024.0 * 1024.0)
                             << "MB | process:" << procMB() << "MB";

                    if (m_provider) {
                        if (!m_availableFields.isEmpty()) {
                            qDebug() << "[MAIN] setColorAttribute:" << m_availableFields.first();
                            m_provider->setColorAttribute(m_availableFields.first());
                            qDebug() << "[MAIN] setColorAttribute done. process:" << procMB() << "MB";
                        }

                        qDebug() << "[MAIN] calling setModel... process:" << procMB() << "MB";
                        m_provider->setModel(&m_model);
                        qDebug() << "[MAIN] setModel done. process:" << procMB() << "MB";
                    }

                    qDebug() << "[MAIN] emit availableFieldsChanged... process:" << procMB() << "MB";
                    emit availableFieldsChanged();
                    qDebug() << "[MAIN] emit availableFieldsChanged done. process:" << procMB() << "MB";

                    qDebug() << "[MAIN] emit boundsChanged... process:" << procMB() << "MB";
                    emit boundsChanged();
                    qDebug() << "[MAIN] emit boundsChanged done. process:" << procMB() << "MB";

                    m_status = QString("Loaded %1 blocks").arg(m_model.size());
                    m_progress = 1.0;
                    qDebug() << "[MAIN] emit progressChanged... process:" << procMB() << "MB";
                    emit progressChanged();
                    qDebug() << "[MAIN] emit progressChanged done. process:" << procMB() << "MB";
                    m_isLoading = false;
                    emit isLoadingChanged();
                    qDebug() << "[MAIN] emit isLoadingChanged done. process:" << procMB() << "MB";
                    emit statusChanged();
                    qDebug() << "[MAIN] done:"
                             << std::chrono::duration_cast<Ms2>(Clock2::now()-tm).count() << "ms";
                });
                qDebug() << "[LOAD] invokeMethod returned:" << (postSuccess ? "SUCCESS" : "FAIL") << ". Worker thread finishing cleanup... Process:" << procMB() << "MB";

            } catch (const std::exception &e) {
                qDebug() << "[LOAD] EXCEPTION:" << e.what();
                QMetaObject::invokeMethod(this, [this, msg = QString(e.what())]() {
                    m_progress = 0.0;
                    emit progressChanged();
                    m_isLoading = false;
                    emit isLoadingChanged();
                    m_status = "Load Error: " + msg;
                    emit statusChanged();
                });
            } catch (...) {
                qDebug() << "[LOAD] UNKNOWN EXCEPTION";
                QMetaObject::invokeMethod(this, [this]() {
                    m_progress = 0.0;
                    emit progressChanged();
                    m_isLoading = false;
                    emit isLoadingChanged();
                    m_status = "Load Error: unknown";
                    emit statusChanged();
                });
            }
        });
    }

    Q_INVOKABLE void loadModel(const QUrl &fileUrl) { preScan(fileUrl); }
    void setProvider(BlockModelProvider* p) { m_provider = p; }

signals:
    void statusChanged();
    void progressChanged();
    void isLoadingChanged();
    void availableFieldsChanged();
    void boundsChanged();

private:
    QFuture<void>       m_loadFuture;
    BlockModelSoA       m_model;
    SpatialIndex        m_index;
    BlockModelProvider* m_provider   = nullptr;
    QString             m_status     = "Ready";
    double              m_progress   = 0.0;
    QStringList         m_availableFields;
    QString             m_lastPath;
    double              m_modelRadius = 1000;
    bool                m_isLoading   = false;
};

int main(int argc, char *argv[])
{
    // Use Fusion style so TextField/Button background: customisation works
    // and the native Windows style doesn't suppress our custom backgrounds.
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
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
