#pragma once

#include "core/ConnectionProfile.h"

#include <QString>

namespace qtrdp {

class RdpFile
{
public:
    static ConnectionProfile read(const QString& path);
    static ConnectionProfile fromText(const QString& text);

    static bool write(const QString& path, const ConnectionProfile& profile);
    static QString toText(const ConnectionProfile& profile);
};

} // namespace qtrdp
