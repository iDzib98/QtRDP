#include "core/ConnectionProfile.h"

namespace qtrdp {
namespace {

template <typename T>
void put(QVariantMap& map, const char* key, const T& value)
{
    map.insert(QString::fromLatin1(key), QVariant::fromValue(value));
}

QString str(const QVariantMap& map, const char* key, const QString& fallback = QString())
{
    const QVariant value = map.value(QString::fromLatin1(key));
    return value.isValid() ? value.toString() : fallback;
}

int num(const QVariantMap& map, const char* key, int fallback = 0)
{
    const QVariant value = map.value(QString::fromLatin1(key));
    return value.isValid() ? value.toInt() : fallback;
}

bool flag(const QVariantMap& map, const char* key, bool fallback = false)
{
    const QVariant value = map.value(QString::fromLatin1(key));
    return value.isValid() ? value.toBool() : fallback;
}

QStringList strList(const QVariantMap& map, const char* key)
{
    return map.value(QString::fromLatin1(key)).toStringList();
}

} // namespace

QVariantMap ConnectionProfile::toVariantMap() const
{
    QVariantMap map;
    put(map, "id", id);
    put(map, "name", name);
    put(map, "protocol", protocol);
    put(map, "host", host);
    put(map, "port", port);
    put(map, "username", username);
    put(map, "domain", domain);
    put(map, "rememberUsername", rememberUsername);
    put(map, "rememberPassword", rememberPassword);
    put(map, "promptForPassword", promptForPassword);
    put(map, "desktopWidth", desktopWidth);
    put(map, "desktopHeight", desktopHeight);
    put(map, "colorDepth", colorDepth);
    put(map, "scaleFactor", scaleFactor);
    put(map, "dynamicResolution", dynamicResolution);
    put(map, "useMultimon", useMultimon);
    put(map, "spanMonitors", spanMonitors);
    put(map, "redirectClipboard", redirectClipboard);
    put(map, "redirectClipboardFiles", redirectClipboardFiles);
    put(map, "redirectAudio", redirectAudio);
    put(map, "redirectMicrophone", redirectMicrophone);
    put(map, "redirectDrives", redirectDrives);
    put(map, "redirectHomeDrive", redirectHomeDrive);
    put(map, "redirectPrinters", redirectPrinters);
    put(map, "drives", drives);
    put(map, "audioFormats", audioFormats);
    put(map, "wallpaper", wallpaper);
    put(map, "themesEnabled", themesEnabled);
    put(map, "fontSmoothing", fontSmoothing);
    put(map, "desktopComposition", desktopComposition);
    put(map, "animations", animations);
    put(map, "shell", shell);
    put(map, "program", program);
    put(map, "remoteProgram", remoteProgram);
    put(map, "gatewayEnabled", gatewayEnabled);
    put(map, "gatewayHostname", gatewayHostname);
    put(map, "gatewayPort", gatewayPort);
    put(map, "gatewayUsername", gatewayUsername);
    put(map, "gatewaySameCredentials", gatewaySameCredentials);
    put(map, "proxyHost", proxyHost);
    put(map, "proxyPort", proxyPort);
    put(map, "proxyUsername", proxyUsername);
    put(map, "proxyPassword", proxyPassword);
    put(map, "proxyType", proxyType);
    put(map, "networkType", networkType);
    put(map, "rdpFilePath", rdpFilePath);
    put(map, "extraOptions", extraOptions);
    return map;
}

ConnectionProfile ConnectionProfile::fromVariantMap(const QVariantMap& map)
{
    ConnectionProfile profile;
    profile.id = str(map, "id");
    profile.name = str(map, "name");
    profile.protocol = str(map, "protocol", QStringLiteral("rdp"));
    profile.host = str(map, "host");
    profile.port = static_cast<quint16>(num(map, "port", 3389));
    profile.username = str(map, "username");
    profile.domain = str(map, "domain");
    profile.rememberUsername = flag(map, "rememberUsername", true);
    profile.rememberPassword = flag(map, "rememberPassword");
    profile.promptForPassword = flag(map, "promptForPassword");
    profile.desktopWidth = num(map, "desktopWidth");
    profile.desktopHeight = num(map, "desktopHeight");
    profile.colorDepth = num(map, "colorDepth", 32);
    profile.scaleFactor = num(map, "scaleFactor", 100);
    profile.dynamicResolution = flag(map, "dynamicResolution", true);
    profile.useMultimon = flag(map, "useMultimon");
    profile.spanMonitors = flag(map, "spanMonitors");
    profile.redirectClipboard = flag(map, "redirectClipboard", true);
    profile.redirectClipboardFiles = flag(map, "redirectClipboardFiles", true);
    profile.redirectAudio = flag(map, "redirectAudio");
    profile.redirectMicrophone = flag(map, "redirectMicrophone");
    profile.redirectDrives = flag(map, "redirectDrives");
    profile.redirectHomeDrive = flag(map, "redirectHomeDrive");
    profile.redirectPrinters = flag(map, "redirectPrinters");
    profile.drives = strList(map, "drives");
    profile.audioFormats = strList(map, "audioFormats");
    profile.wallpaper = str(map, "wallpaper");
    profile.themesEnabled = flag(map, "themesEnabled", true);
    profile.fontSmoothing = flag(map, "fontSmoothing", true);
    profile.desktopComposition = flag(map, "desktopComposition", true);
    profile.animations = flag(map, "animations", true);
    profile.shell = str(map, "shell");
    profile.program = str(map, "program");
    profile.remoteProgram = str(map, "remoteProgram");
    profile.gatewayEnabled = flag(map, "gatewayEnabled");
    profile.gatewayHostname = str(map, "gatewayHostname");
    profile.gatewayPort = num(map, "gatewayPort");
    profile.gatewayUsername = str(map, "gatewayUsername");
    profile.gatewaySameCredentials = flag(map, "gatewaySameCredentials", true);
    profile.proxyHost = str(map, "proxyHost");
    profile.proxyPort = num(map, "proxyPort");
    profile.proxyUsername = str(map, "proxyUsername");
    profile.proxyPassword = str(map, "proxyPassword");
    profile.proxyType = str(map, "proxyType");
    profile.networkType = str(map, "networkType", QStringLiteral("lan"));
    profile.rdpFilePath = str(map, "rdpFilePath");
    profile.extraOptions = map.value(QStringLiteral("extraOptions")).toMap();
    return profile;
}

} // namespace qtrdp
