#include "BlockModel/SubprocessReader.h"
#include "BlockModel/MicromineReader.h"
#include <QProcess>
#include <QCoreApplication>
#include <QDataStream>
#include <QRegularExpression>
#include <iostream>

namespace Mining {

QStringList SubprocessReader::getFields(const QString& datPath) {
    QString appDir = QCoreApplication::applicationDirPath();
    QProcess p;
    p.setWorkingDirectory(appDir);
    p.start(appDir + "/MicromineProxy.exe", QStringList() << datPath << "--fields-only");
    p.waitForFinished(10000);
    return QString::fromUtf8(p.readAllStandardOutput()).split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
}

BlockModelSoA SubprocessReader::load(const QString& datPath, const std::map<std::string, std::string>& mapping) {
    QString appDir = QCoreApplication::applicationDirPath();
    QProcess* proxy = new QProcess();
    proxy->setWorkingDirectory(appDir);
    proxy->start(appDir + "/MicromineProxy.exe", QStringList() << datPath);
    if (!proxy->waitForStarted()) throw std::runtime_error("No Proxy.");

    BlockModelSoA model;
    QDataStream stream(proxy);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);

    // 1. Magic & Counts
    char magic[4];
    while(proxy->bytesAvailable() < 12) { if(!proxy->waitForReadyRead(5000)) break; }
    stream.readRawData(magic, 4);
    
    int32_t recordCount, fieldCount;
    stream >> recordCount >> fieldCount;

    // 2. Field Names (Using stream only to avoid buffer conflict)
    std::vector<QString> proxyFields;
    for (int i = 0; i < fieldCount; ++i) {
        int32_t nameLen;
        stream >> nameLen;
        QByteArray nameBytes(nameLen, 0);
        stream.readRawData(nameBytes.data(), nameLen);
        proxyFields.push_back(QString::fromUtf8(nameBytes));
    }

    auto find_idx = [&](const std::string& key) -> int {
        if (!mapping.count(key)) return -1;
        QString target = QString::fromStdString(mapping.at(key)).toUpper();
        for(int i=0; i<fieldCount; ++i) if(proxyFields[i].toUpper() == target) return i;
        return -1;
    };

    int ix = find_idx("X"), iy = find_idx("Y"), iz = find_idx("Z");
    int ixs = find_idx("XSPAN"), iys = find_idx("YSPAN"), izs = find_idx("ZSPAN");

    for (int r = 0; r < recordCount; ++r) {
        // PER-RECORD SYNC
        uint32_t marker = 0;
        while(proxy->bytesAvailable() < 4) { if(!proxy->waitForReadyRead(5000)) break; }
        stream >> marker;
        if (marker != 0xAA55AA55) break;

        std::vector<double> row(fieldCount);
        for (int f = 0; f < fieldCount; ++f) {
            while(proxy->bytesAvailable() < 8) { if(!proxy->waitForReadyRead(5000)) break; }
            stream >> row[f];
        }

        if (ix >= 0 && iy >= 0 && iz >= 0 && std::isfinite(row[ix])) {
            model.x.push_back(row[ix]); 
            model.y.push_back(row[iy]); 
            model.z.push_back(row[iz]);
            model.x_span.push_back((float)((ixs >= 0) ? row[ixs] : 10.0));
            model.y_span.push_back((float)((iys >= 0) ? row[iys] : 10.0));
            model.z_span.push_back((float)((izs >= 0) ? row[izs] : 5.0));
            model.visible.push_back(1);
            model.mined_state.push_back(0);
            
            for (auto const& [internal, _] : mapping) {
                if (internal != "X" && internal != "Y" && internal != "Z" && internal != "XSPAN" && internal != "YSPAN" && internal != "ZSPAN") {
                    int fidx = find_idx(internal);
                    if (fidx >= 0) model.attributes[internal].push_back((float)row[fidx]);
                }
            }
        }
    }

    proxy->waitForFinished();
    delete proxy;

    // --- MANDATORY GEOMETRIC PROCESSING ---
    if (!model.empty()) {
        MicromineReader::center_model(model);
        // Morton keys will be implemented in Step 2.2
    }

    return model;
}

} // namespace Mining
