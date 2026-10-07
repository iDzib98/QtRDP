#pragma once

#include "core/ConnectionProfile.h"
#include "core/SessionBackend.h"

#include <QList>
#include <QString>

namespace qtrdp {

class SessionRegistry
{
public:
    using Factory = SessionBackend* (*)(QObject* parent);

    static void registerProtocol(const QString& protocol, Factory factory, QString displayName);
    static bool isSupported(const QString& protocol);
    static QStringList protocols();
    static SessionBackend* create(const QString& protocol, QObject* parent);
    static QString displayName(const QString& protocol);

private:
    struct Entry
    {
        Factory factory = nullptr;
        QString displayName;
    };
    static QList<QPair<QString, Entry>>& entries();
};

} // namespace qtrdp
