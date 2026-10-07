#pragma once

#include <QString>

namespace qtrdp {

// FreeRDP espera el usuario y el dominio por separado; si el usuario trae el
// dominio dentro («LAB\soporte» o «soporte@lab.local») hay que separarlo o la
// autenticación falla con ERRCONNECT_LOGON_FAILURE.
struct NormalizedCredentials
{
    QString username;
    QString domain;
};

NormalizedCredentials normalizeCredentials(const QString& username, const QString& domain);

} // namespace qtrdp
