#include "core/SessionRegistry.h"

#include "core/backends/rdp/RdpSession.h"

namespace qtrdp {



QList<QPair<QString, SessionRegistry::Entry>>& SessionRegistry::entries()
{
    static QList<QPair<QString, Entry>> registry;
    return registry;
}

void SessionRegistry::registerProtocol(const QString& protocol, Factory factory, QString displayName)
{
    Entry entry;
    entry.factory = factory;
    entry.displayName = std::move(displayName);
    entries().append(qMakePair(protocol, entry));
}

bool SessionRegistry::isSupported(const QString& protocol)
{
    for (const auto& item : entries())
    {
        if (item.first == protocol)
            return true;
    }
    return false;
}

QStringList SessionRegistry::protocols()
{
    QStringList result;
    for (const auto& item : entries())
        result.append(item.first);
    return result;
}

SessionBackend* SessionRegistry::create(const QString& protocol, QObject* parent)
{
    for (const auto& item : entries())
    {
        if (item.first == protocol)
            return item.second.factory(parent);
    }
    return nullptr;
}

QString SessionRegistry::displayName(const QString& protocol)
{
    for (const auto& item : entries())
    {
        if (item.first == protocol)
            return item.second.displayName;
    }
    return protocol;
}

namespace {

SessionBackend* createRdp(QObject* parent)
{
    return new RdpSession(parent);
}

struct Registrar
{
    Registrar()
    {
        SessionRegistry::registerProtocol(QStringLiteral("rdp"), &createRdp,
                                          QStringLiteral("RDP"));
    }
};

const Registrar registrar;

} // namespace

} // namespace qtrdp
