#pragma once

#include "core/ConnectionProfile.h"

#include <QList>
#include <QString>

namespace qtrdp {

class ProfileStore
{
public:
    explicit ProfileStore(const QString& filePath = QString());

    static QString defaultFilePath();

    QList<ConnectionProfile> profiles() const;
    int count() const;
    ConnectionProfile profile(const QString& id) const;
    int indexOf(const QString& id) const;
    bool contains(const QString& id) const;

    bool add(const ConnectionProfile& profile);
    bool update(const ConnectionProfile& profile);
    bool remove(const QString& id);
    bool move(const QString& id, int offset);

    bool importRdpFile(const QString& path);
    bool exportRdpFile(const QString& id, const QString& path) const;

    QString filePath() const;

private:
    void load();
    bool flush();

    QString m_filePath;
    QList<ConnectionProfile> m_profiles;
};

} // namespace qtrdp
