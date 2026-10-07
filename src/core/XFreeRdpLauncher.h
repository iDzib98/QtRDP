#pragma once

#include "core/ConnectionProfile.h"
#include "core/SessionBackend.h"

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QProcess;
QT_END_NAMESPACE

namespace qtrdp {

class XFreeRdpLauncher final : public QObject
{
    Q_OBJECT

public:
    explicit XFreeRdpLauncher(QObject* parent = nullptr);
    ~XFreeRdpLauncher() override;

    static QString programPath();
    static QStringList arguments(const ConnectionProfile& profile,
                                 const SessionBackend::Credentials& credentials);

    bool start(const ConnectionProfile& profile,
               const SessionBackend::Credentials& credentials);
    void stop();
    bool isRunning() const;

    struct ConnectionError
    {
        QString title;
        QString friendlyMessage;
        QString technicalDetails;
        bool isFatal = true;
    };

    // Analiza la salida de xfreerdp y devuelve un error estructurado.
    static ConnectionError analyzeOutput(const QString& output, int exitCode);

signals:
    void error(const QString& message);
    void connectionError(const qtrdp::XFreeRdpLauncher::ConnectionError& error);
    void finished(int exitCode, bool normalExit);
    void outputReceived(const QString& line);

private:
    QProcess* m_process = nullptr;
    QStringList m_outputLines;
    bool m_connected = false;
};

} // namespace qtrdp
