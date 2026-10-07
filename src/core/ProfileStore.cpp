#include "core/ProfileStore.h"

#include "core/RdpFile.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace qtrdp {

namespace {

constexpr int kMaxProfiles = 256;

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString displayNameOrDefault(const ConnectionProfile& profile,
                             const QString& fallback = QString())
{
    const QString label = profile.displayName();
    if (!label.isEmpty())
        return label;
    return fallback.isEmpty() ? QStringLiteral("Perfil sin nombre") : fallback;
}

} // namespace

ProfileStore::ProfileStore(const QString& filePath)
    : m_filePath(filePath.isEmpty() ? defaultFilePath() : filePath)
{
    load();
}

QString ProfileStore::defaultFilePath()
{
    const QString directory =
	    QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("perfiles.ini"));
}

QString ProfileStore::filePath() const
{
    return m_filePath;
}

void ProfileStore::load()
{
    m_profiles.clear();

    QSettings settings(m_filePath, QSettings::IniFormat);
    const int total = settings.beginReadArray(QStringLiteral("perfiles"));
    for (int index = 0; index < total; ++index)
    {
        settings.setArrayIndex(index);

        QVariantMap data;
        const QStringList keys = settings.childKeys();
        for (const QString& key : keys)
            data.insert(key, settings.value(key));

        ConnectionProfile profile = ConnectionProfile::fromVariantMap(data);
        if (profile.id.isEmpty())
            profile.id = newId();
        if (profile.name.isEmpty())
            profile.name = displayNameOrDefault(profile);

        m_profiles.append(profile);
    }
    settings.endArray();
}

bool ProfileStore::flush()
{
    const QString directory = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(directory))
        return false;

    QSettings settings(m_filePath, QSettings::IniFormat);
    settings.clear();
    settings.beginWriteArray(QStringLiteral("perfiles"), m_profiles.size());

    for (int index = 0; index < m_profiles.size(); ++index)
    {
        settings.setArrayIndex(index);
        const QVariantMap data = m_profiles.at(index).toVariantMap();
        for (auto it = data.constBegin(); it != data.constEnd(); ++it)
            settings.setValue(it.key(), it.value());
    }

    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QList<ConnectionProfile> ProfileStore::profiles() const
{
    return m_profiles;
}

int ProfileStore::count() const
{
    return static_cast<int>(m_profiles.size());
}

int ProfileStore::indexOf(const QString& id) const
{
    for (int index = 0; index < m_profiles.size(); ++index)
    {
        if (m_profiles.at(index).id == id)
            return index;
    }
    return -1;
}

bool ProfileStore::contains(const QString& id) const
{
    return indexOf(id) >= 0;
}

ConnectionProfile ProfileStore::profile(const QString& id) const
{
    const int index = indexOf(id);
    if (index < 0)
        return {};
    return m_profiles.at(index);
}

bool ProfileStore::add(const ConnectionProfile& profile)
{
    if (m_profiles.size() >= kMaxProfiles)
        return false;

    ConnectionProfile stored = profile;
    if (stored.id.isEmpty() || contains(stored.id))
        stored.id = newId();
    if (stored.name.isEmpty())
        stored.name = displayNameOrDefault(stored);

    m_profiles.append(stored);
    return flush();
}

bool ProfileStore::update(const ConnectionProfile& profile)
{
    const int index = indexOf(profile.id);
    if (index < 0)
        return false;

    m_profiles[index] = profile;
    if (m_profiles.at(index).name.isEmpty())
        m_profiles[index].name = displayNameOrDefault(profile);
    return flush();
}

bool ProfileStore::remove(const QString& id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;

    m_profiles.removeAt(index);
    return flush();
}

bool ProfileStore::move(const QString& id, int offset)
{
    const int index = indexOf(id);
    if ((index < 0) || (offset == 0))
        return false;

    const int target = index + offset;
    if ((target < 0) || (target >= m_profiles.size()))
        return false;

    const ConnectionProfile item = m_profiles.takeAt(index);
    m_profiles.insert(target, item);
    return flush();
}

bool ProfileStore::importRdpFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;

    ConnectionProfile profile = RdpFile::read(info.absoluteFilePath());
    if (!profile.isValid())
        return false;

    // Evita duplicar el mismo archivo importado dos veces.
    for (const ConnectionProfile& existing : std::as_const(m_profiles))
    {
        if ((existing.host == profile.host) && (existing.port == profile.port) &&
            (existing.username == profile.username))
        {
            m_profiles[indexOf(existing.id)] = profile;
            return flush();
        }
    }

    if (m_profiles.size() >= kMaxProfiles)
        return false;

    if (profile.id.isEmpty() || contains(profile.id))
        profile.id = newId();
    if (profile.name.isEmpty())
        profile.name = displayNameOrDefault(profile, info.completeBaseName());

    m_profiles.append(profile);
    return flush();
}

bool ProfileStore::exportRdpFile(const QString& id, const QString& path) const
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    return RdpFile::write(path, m_profiles.at(index));
}

} // namespace qtrdp
