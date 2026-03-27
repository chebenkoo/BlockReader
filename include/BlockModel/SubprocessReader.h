#pragma once

#include "BlockModel.h"
#include <QStringList>
#include <map>
#include <string>

namespace Mining {

/**
 * @brief Bridge to MicromineProxy.exe for reliable .DAT reading.
 */
class SubprocessReader {
public:
    /**
     * @brief Gets all numeric field names from the DAT file.
     */
    static QStringList getFields(const QString& datPath);

    /**
     * @brief Loads the full model using MicromineProxy.exe.
     */
    static BlockModelSoA load(
        const QString& datPath, 
        const std::map<std::string, std::string>& mapping
    );
};

} // namespace Mining
