#pragma once

#include <QString>
#include <QVariantMap>

namespace qtrdp {

struct ConnectionProfile
{
    QString id;
    QString name;
    QString protocol = QStringLiteral("rdp");

    QString host;
    quint16 port = 3389;

    QString username;
    QString domain;
    bool rememberUsername = true;
    bool rememberPassword = false;
    bool promptForPassword = false;

    int desktopWidth = 0;
    int desktopHeight = 0;
    int colorDepth = 32;
    int scaleFactor = 100;
    bool dynamicResolution = true;
    bool useMultimon = false;
    bool spanMonitors = false;

    bool redirectClipboard = true;
    bool redirectClipboardFiles = true;
    bool redirectAudio = false;
    bool redirectMicrophone = false;
    bool redirectDrives = false;
    bool redirectHomeDrive = false;
    bool redirectPrinters = false;
    QStringList drives;
    QStringList audioFormats;

    QString wallpaper;
    bool themesEnabled = true;
    bool fontSmoothing = true;
    bool desktopComposition = true;
    bool animations = true;

    QString shell;
    QString program;
    QString remoteProgram;

    QString gatewayHostname;
    QString gatewayUsername;
    int gatewayPort = 0;
    bool gatewayEnabled = false;
    bool gatewaySameCredentials = true;

    QString proxyHost;
    int proxyPort = 0;
    QString proxyUsername;
    QString proxyPassword;
    QString proxyType;

    QString networkType = QStringLiteral("lan");
    QString rdpFilePath;
    QVariantMap extraOptions;

    bool isValid() const
    {
        return !host.trimmed().isEmpty() && port > 0;
    }

    QString displayName() const
    {
        if (!name.isEmpty())
            return name;
        if (username.isEmpty())
            return host;
        if (domain.isEmpty())
            return username;
        return QStringLiteral("%1\\%2").arg(domain, username);
    }

    QVariantMap toVariantMap() const;
    static ConnectionProfile fromVariantMap(const QVariantMap& map);
};

} // namespace qtrdp
