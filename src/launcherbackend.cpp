#include "launcherbackend.h"

#include "aliasesmodel.h"
#include "historymodel.h"
#include "terminal.h"

#include <QProcess>
#include <QTimer>
#include <QVariantMap>

namespace {

constexpr int kCaptureTimeoutMs = 10000;
constexpr int kOutputTailLines = 32;

// Mirrors Python's `(stdout + stderr).strip().splitlines()` then keeping
// only the last 32 lines, joined back with newlines.
QString formatOutput(const QString &combined) {
    const QString stripped = combined.trimmed();
    if (stripped.isEmpty()) {
        return QStringLiteral("(no output)");
    }
    QStringList lines = stripped.split(QLatin1Char('\n'));
    if (lines.size() > kOutputTailLines) {
        lines = lines.mid(lines.size() - kOutputTailLines);
    }
    return lines.join(QLatin1Char('\n'));
}

}

LauncherBackend::LauncherBackend(const QString &aliasesFile, const QString &historyFile, QObject *parent)
    : QObject(parent),
      m_history(new HistoryModel(historyFile, 200, this)),
      m_aliases(new AliasesModel(aliasesFile, this)),
      m_shell(qEnvironmentVariable("SHELL", "/bin/bash")),
      m_hasTerminal(Terminal::find().has_value()) {}

QObject *LauncherBackend::history() const { return m_history; }
QObject *LauncherBackend::aliases() const { return m_aliases; }
QString LauncherBackend::shell() const { return m_shell; }
bool LauncherBackend::hasTerminal() const { return m_hasTerminal; }

QString LauncherBackend::expand(const QString &query) const {
    const QString resolved = m_aliases->resolve(query.trimmed());
    // Matches Python's `resolved if resolved is not None else query`: only
    // a genuine "no such alias" (null) falls back, not an empty command.
    return resolved.isNull() ? query : resolved;
}

void LauncherBackend::run(const QString &command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    // Matches Python's `resolve(command) or command`: an alias resolving to
    // an empty command falls back to the literal command too, same as "not found".
    const QString aliasResolved = m_aliases->resolve(trimmed);
    const QString resolved = aliasResolved.isEmpty() ? trimmed : aliasResolved;

    if (Terminal::launch(resolved, m_shell)) {
        emit commandStarted(trimmed);
        emit commandFinished(-1);
    } else {
        runBackground(resolved);
    }
    m_history->add(trimmed);
}

void LauncherBackend::runBackground(const QString &command) {
    emit commandStarted(command);
    emit statusChanged(QStringLiteral("running: %1").arg(command));
    if (QProcess::startDetached(m_shell, {"-lc", command})) {
        emit commandFinished(-1);
    } else {
        const QString msg = QStringLiteral("error: failed to launch shell");
        emit outputReceived(msg);
        emit statusChanged(msg);
        emit commandFinished(-1);
    }
}

void LauncherBackend::runCapture(const QString &command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (m_captureProcess) {
        // A capture is already in flight; Python's blocking implementation
        // never had to handle re-entrancy, so we just ignore the new call.
        return;
    }

    const QString aliasResolved = m_aliases->resolve(trimmed);
    const QString resolved = aliasResolved.isEmpty() ? trimmed : aliasResolved;

    auto *proc = new QProcess(this);
    m_captureProcess = proc;
    m_captureTimedOut = false;

    emit commandStarted(trimmed);
    emit statusChanged(QStringLiteral("running: %1").arg(resolved));

    connect(proc, &QProcess::errorOccurred, this, [this, proc, trimmed](QProcess::ProcessError error) {
        if (m_captureProcess != proc || error != QProcess::FailedToStart) {
            return;
        }
        const QString msg = QStringLiteral("error: %1").arg(proc->errorString());
        emit outputReceived(msg);
        emit statusChanged(msg);
        emit commandFinished(-1);
        m_history->add(trimmed);
        proc->deleteLater();
        m_captureProcess = nullptr;
    });

    connect(proc, &QProcess::finished, this,
            [this, proc, trimmed, resolved](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_captureProcess != proc) {
                    return;
                }
                if (m_captureTimedOut) {
                    emit outputReceived(QStringLiteral("error: timed out after 10s"));
                    emit commandFinished(-1);
                } else {
                    const QString combined = QString::fromUtf8(proc->readAllStandardOutput()) +
                                              QString::fromUtf8(proc->readAllStandardError());
                    emit outputReceived(formatOutput(combined));
                    const int rc = (exitStatus == QProcess::NormalExit) ? exitCode : -1;
                    emit statusChanged(rc == 0 ? QStringLiteral("ok: %1").arg(resolved)
                                                : QStringLiteral("exit %1: %2").arg(rc).arg(resolved));
                    emit commandFinished(rc);
                }
                m_history->add(trimmed);
                proc->deleteLater();
                m_captureProcess = nullptr;
            });

    proc->start(m_shell, {"-lc", resolved});

    QTimer::singleShot(kCaptureTimeoutMs, proc, [this, proc] {
        if (m_captureProcess == proc) {
            m_captureTimedOut = true;
            proc->kill();
        }
    });
}

QVariantList LauncherBackend::suggestHistory(const QString &prefix) const {
    QVariantList result;
    const QVector<HistoryEntry> matches = m_history->search(prefix);
    result.reserve(matches.size());
    for (const HistoryEntry &item : matches) {
        result.append(QVariantMap{{"command", item.command}, {"count", item.count}});
    }
    return result;
}

QVariantList LauncherBackend::suggestAliases(const QString &prefix) const {
    QVariantList result;
    const QVector<AliasEntry> matches = m_aliases->search(prefix);
    result.reserve(matches.size());
    for (const AliasEntry &item : matches) {
        result.append(QVariantMap{
            {"name", item.name},
            {"command", item.command},
            {"description", item.description},
        });
    }
    return result;
}
