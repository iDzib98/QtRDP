#include "core/RdpFile.h"

#include <QFile>
#include <QSize>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>

namespace qtrdp {

namespace {

QString boolText(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

bool parseBool(const QString& value, bool fallback)
{
    const QString normalized = value.trimmed().toLower();
    if ((normalized == QLatin1String("0")) || (normalized == QLatin1String("false")) ||
        (normalized == QLatin1String("no")))
        return false;
    if ((normalized == QLatin1String("1")) || (normalized == QLatin1String("true")) ||
        (normalized == QLatin1String("yes")))
        return true;
    return fallback;
}

void appendOption(QStringList& lines, const QString& key, const QString& value)
{
    lines.append(QStringLiteral("%1:%2").arg(key, value));
}

void appendFlag(QStringList& lines, const QString& key, bool value)
{
    lines.append(QStringLiteral("%1:%2").arg(key, boolText(value)));
}

QString screenModeValue(const ConnectionProfile& profile)
{
    if (profile.useMultimon)
        return QStringLiteral("2");
    if (profile.dynamicResolution && ((profile.desktopWidth <= 0) || (profile.desktopHeight <= 0)))
        return QStringLiteral("2");
    return QStringLiteral("1");
}

QSize parseGeometry(const QString& value)
{
    const QStringList parts = value.split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return {};

    bool widthOk = false;
    bool heightOk = false;
    const int width = parts.at(0).trimmed().toInt(&widthOk);
    const int height = parts.at(1).trimmed().toInt(&heightOk);
    if (!widthOk || !heightOk || (width <= 0) || (height <= 0))
        return {};

    return QSize(width, height);
}

QString audioModeValue(const ConnectionProfile& profile)
{
    if (profile.redirectAudio)
        return profile.redirectMicrophone ? QStringLiteral("2") : QStringLiteral("1");
    return QStringLiteral("0");
}

} // namespace

ConnectionProfile RdpFile::fromText(const QString& text)
{
    ConnectionProfile profile;

    QString host;
    QString fullAddress;

    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (QString& line : lines)
        line = line.trimmed();

    for (const QString& line : lines)
    {
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
            continue;

        const int separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0)
            continue;

        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1).trimmed();

        if (key == QLatin1String("full address"))
            fullAddress = value;
        else if (key == QLatin1String("server address"))
            host = value;
        else if (key == QLatin1String("server port"))
            profile.port = static_cast<quint16>(value.toUShort());
        else if (key == QLatin1String("user name"))
            profile.username = value;
        else if (key == QLatin1String("domain"))
            profile.domain = value;
        else if (key == QLatin1String("alternate shell"))
            profile.shell = value;
        else if (key == QLatin1String("shell working directory"))
            profile.extraOptions.insert(QStringLiteral("shellWorkingDirectory"), value);
        else if (key == QLatin1String("remoteapplicationprogram"))
            profile.remoteProgram = value;
        else if (key == QLatin1String("remoteapplicationmode"))
            profile.extraOptions.insert(key, value);
        else if (key == QLatin1String("desktopwidth"))
            profile.desktopWidth = qMax(0, value.toInt());
        else if (key == QLatin1String("desktopheight"))
            profile.desktopHeight = qMax(0, value.toInt());
        else if (key == QLatin1String("color depth"))
            profile.colorDepth = qMax(0, value.toInt());
        else if (key == QLatin1String("desktop scale factor"))
            profile.scaleFactor = value.toInt() > 0 ? value.toInt() : 100;
        else if (key == QLatin1String("smart sizing"))
            profile.dynamicResolution = parseBool(value, profile.dynamicResolution);
        else if (key == QLatin1String("dynamic resolution"))
            profile.dynamicResolution = parseBool(value, profile.dynamicResolution);
        else if (key == QLatin1String("use multimon"))
            profile.useMultimon = parseBool(value, profile.useMultimon);
        else if (key == QLatin1String("span monitors"))
            profile.spanMonitors = parseBool(value, profile.spanMonitors);
        else if (key == QLatin1String("screen mode id"))
            profile.dynamicResolution = value == QLatin1String("2");
        else if (key == QLatin1String("clipboard redirection"))
            profile.redirectClipboard = parseBool(value, profile.redirectClipboard);
        else if (key == QLatin1String("disable clipboard redirection"))
            profile.redirectClipboard = !parseBool(value, true);
        else if (key == QLatin1String("audiomode"))
        {
            profile.redirectAudio = value != QLatin1String("0");
            profile.redirectMicrophone = value == QLatin1String("2");
        }
        else if (key == QLatin1String("redirectdrives"))
            profile.redirectDrives = parseBool(value, profile.redirectDrives);
        else if (key == QLatin1String("redirect home drive"))
            profile.redirectHomeDrive = parseBool(value, profile.redirectHomeDrive);
        else if (key == QLatin1String("redirectprinters"))
            profile.redirectPrinters = parseBool(value, profile.redirectPrinters);
        else if (key == QLatin1String("drivestoredirect"))
            profile.drives = value.split(QRegularExpression(QStringLiteral("[;:]")),
                                         Qt::SkipEmptyParts);
        else if (key == QLatin1String("wallpaper"))
            profile.wallpaper = value;
        else if (key == QLatin1String("allow themes"))
            profile.themesEnabled = parseBool(value, profile.themesEnabled);
        else if (key == QLatin1String("allow font smoothing"))
            profile.fontSmoothing = parseBool(value, profile.fontSmoothing);
        else if (key == QLatin1String("allow desktop composition"))
            profile.desktopComposition = parseBool(value, profile.desktopComposition);
        else if (key == QLatin1String("animate menu and windows"))
            profile.animations = parseBool(value, profile.animations);
        else if (key == QLatin1String("gateway enabled"))
            profile.gatewayEnabled = parseBool(value, profile.gatewayEnabled);
        else if (key == QLatin1String("gatewayusagemethod"))
        {
            // 0 = no usar gateway, 1 = mismas credenciales, 2 = otro usuario.
            profile.gatewayEnabled = value != QLatin1String("0");
            profile.gatewaySameCredentials = value != QLatin1String("2");
        }
        else if (key == QLatin1String("gatewayhostname"))
            profile.gatewayHostname = value;
        else if (key == QLatin1String("gatewayusername"))
            profile.gatewayUsername = value;
        else if (key == QLatin1String("gatewayport"))
            profile.gatewayPort = value.toInt();
        else if (key == QLatin1String("username is required"))
            profile.promptForPassword = parseBool(value, false);
        else if (key == QLatin1String("prompt for credentials"))
            profile.promptForPassword = parseBool(value, profile.promptForPassword);
        else if (key == QLatin1String("proxyserver"))
            profile.proxyHost = value;
        else if (key == QLatin1String("proxyport"))
            profile.proxyPort = value.toInt();
        else if (key == QLatin1String("proxyusername"))
            profile.proxyUsername = value;
        else if (key == QLatin1String("proxypassword"))
            profile.proxyPassword = value;
        else if (key == QLatin1String("networkautologon"))
            profile.networkType = parseBool(value, false) ? QStringLiteral("bypassserver")
                                                         : QStringLiteral("lan");
        else
            profile.extraOptions.insert(key, value);
    }

    const QString address = !fullAddress.isEmpty() ? fullAddress : host;
    if (!address.isEmpty())
    {
        // mstsc permite "host:puerto" dentro de full address.
        const QStringList parts = address.split(QLatin1Char(':'));
        profile.host = parts.first().trimmed();
        if ((parts.size() > 1) && (profile.port == 3389))
        {
            bool ok = false;
            const int port = parts.at(1).trimmed().toInt(&ok);
            if (ok && (port > 0) && (port < 65536))
                profile.port = static_cast<quint16>(port);
        }
    }

    if (profile.port == 0)
        profile.port = 3389;

    if (!profile.name.isEmpty())
    {
        profile.id = profile.name;
    }
    else if (!profile.rdpFilePath.isEmpty())
    {
        profile.id = QFileInfo(profile.rdpFilePath).completeBaseName();
        profile.name = profile.id;
    }

    return profile;
}

ConnectionProfile RdpFile::read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString content = stream.readAll();
    file.close();

    ConnectionProfile profile = fromText(content);
    profile.rdpFilePath = QFileInfo(path).absoluteFilePath();
    if (profile.id.isEmpty())
        profile.id = QFileInfo(path).completeBaseName();
    if (profile.name.isEmpty())
        profile.name = QFileInfo(path).completeBaseName();
    return profile;
}

QString RdpFile::toText(const ConnectionProfile& profile)
{
    QStringList lines;
    lines.append(QStringLiteral("screen mode id:%1").arg(screenModeValue(profile)));
    appendOption(lines, QStringLiteral("desktopwidth"),
                 profile.desktopWidth > 0 ? QString::number(profile.desktopWidth)
                                          : QStringLiteral("0"));
    appendOption(lines, QStringLiteral("desktopheight"),
                 profile.desktopHeight > 0 ? QString::number(profile.desktopHeight)
                                           : QStringLiteral("0"));
    appendOption(lines, QStringLiteral("color depth"), QString::number(profile.colorDepth));
    appendOption(lines, QStringLiteral("desktop scale factor"),
                 QString::number(profile.scaleFactor));
    appendFlag(lines, QStringLiteral("smart sizing"), profile.dynamicResolution);
    appendFlag(lines, QStringLiteral("use multimon"), profile.useMultimon);
    appendFlag(lines, QStringLiteral("span monitors"), profile.spanMonitors);

    appendOption(lines, QStringLiteral("full address"),
                 profile.port == 3389
	                     ? profile.host
	                     : QStringLiteral("%1:%2").arg(profile.host).arg(profile.port));

    if (profile.username.isEmpty())
        lines.append(QStringLiteral("user name:"));
    else
        appendOption(lines, QStringLiteral("user name"), profile.username);

    appendOption(lines, QStringLiteral("domain"), profile.domain);

    appendFlag(lines, QStringLiteral("redirectclipboard"), profile.redirectClipboard);
    appendOption(lines, QStringLiteral("audiomode"), audioModeValue(profile));
    appendFlag(lines, QStringLiteral("redirectdrives"), profile.redirectDrives);
    appendFlag(lines, QStringLiteral("redirect home drive"), profile.redirectHomeDrive);
    appendFlag(lines, QStringLiteral("redirectprinters"), profile.redirectPrinters);
    if (!profile.drives.isEmpty())
        appendOption(lines, QStringLiteral("drivestoredirect"),
                     profile.drives.join(QLatin1Char(';')));

    appendFlag(lines, QStringLiteral("allow themes"), profile.themesEnabled);
    appendFlag(lines, QStringLiteral("allow font smoothing"), profile.fontSmoothing);
    appendFlag(lines, QStringLiteral("allow desktop composition"), profile.desktopComposition);
    appendFlag(lines, QStringLiteral("animate menu and windows"), profile.animations);
    appendFlag(lines, QStringLiteral("username is required"), profile.promptForPassword);

    if (profile.gatewayEnabled && !profile.gatewayHostname.isEmpty())
    {
        appendFlag(lines, QStringLiteral("gateway enabled"), true);
        appendOption(lines, QStringLiteral("gatewayhostname"), profile.gatewayHostname);
        if (!profile.gatewayUsername.isEmpty())
            appendOption(lines, QStringLiteral("gatewayusername"), profile.gatewayUsername);
        appendOption(lines, QStringLiteral("gatewayport"),
                     QString::number(profile.gatewayPort > 0 ? profile.gatewayPort : 443));
    }

    if (!profile.shell.isEmpty())
        appendOption(lines, QStringLiteral("alternate shell"), profile.shell);
    if (!profile.remoteProgram.isEmpty())
    {
        appendOption(lines, QStringLiteral("remoteapplicationprogram"), profile.remoteProgram);
        appendFlag(lines, QStringLiteral("remoteapplicationmode"), true);
    }

    if (!profile.wallpaper.isEmpty())
        appendOption(lines, QStringLiteral("wallpaper"), profile.wallpaper);

    for (auto it = profile.extraOptions.constBegin(); it != profile.extraOptions.constEnd(); ++it)
        appendOption(lines, it.key(), it.value().toString());

    QString text = lines.join(QStringLiteral("\r\n"));
    text.append(QStringLiteral("\r\n"));
    return text;
}

bool RdpFile::write(const QString& path, const ConnectionProfile& profile)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream.setGenerateByteOrderMark(false);
    stream << toText(profile);
    stream.flush();
    file.close();
    return true;
}

} // namespace qtrdp
