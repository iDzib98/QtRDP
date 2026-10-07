#pragma once

#include "core/ConnectionProfile.h"

#include <QString>

namespace qtrdp {

class CredentialStore
{
public:
    virtual ~CredentialStore() = default;

    virtual bool isAvailable() const = 0;
    virtual QString backendName() const = 0;

    // Ruta del archivo de respaldo; vacía cuando se usa el almacén de secretos.
    virtual QString filePath() const
    {
        return {};
    }

    virtual QString read(const QString& credentialId, QString* error = nullptr) const = 0;
    virtual bool write(const QString& credentialId, const QString& secret,
                       QString* error = nullptr) = 0;
    virtual bool remove(const QString& credentialId, QString* error = nullptr) = 0;

    static CredentialStore* instance();
    static void setInstance(CredentialStore* store);

    // Respaldo sin cifrar; se usa cuando no hay almacén de secretos disponible.
    static CredentialStore* createFileStore();

    // Identificador estable por servidor y usuario, similar al que guarda mstsc.
    static QString credentialId(const ConnectionProfile& profile);
};

} // namespace qtrdp
