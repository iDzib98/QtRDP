#include "core/CredentialFormat.h"

namespace qtrdp {

NormalizedCredentials normalizeCredentials(const QString& username, const QString& domain)
{
    NormalizedCredentials result;
    result.username = username.trimmed();
    result.domain = domain.trimmed();

    // «usuario@dominio» es la forma UPN que acepta NLA.
    const int at = result.username.lastIndexOf(QLatin1Char('@'));
    if (at > 0)
    {
        const QString upnDomain = result.username.mid(at + 1).trimmed();
        if (result.domain.isEmpty())
            result.domain = upnDomain;
        result.username = result.username.left(at).trimmed();
    }

    // «DOMINIO\usuario» y «.\usuario» (cuenta local del propio equipo).
    // El dominio es lo que hay antes de la primera barra y nunca puede
    // contenerla; el usuario es lo que hay después de la última.
    const int firstSlash = result.username.indexOf(QLatin1Char('\\'));
    const int lastSlash = result.username.lastIndexOf(QLatin1Char('\\'));
    if (lastSlash >= 0)
    {
        if (result.domain.isEmpty())
        {
            const QString prefix = result.username.left(firstSlash).trimmed();
            if ((prefix != QLatin1String(".")) && !prefix.isEmpty())
                result.domain = prefix;
        }

        result.username = result.username.mid(lastSlash + 1).trimmed();
    }

    return result;
}

} // namespace qtrdp
