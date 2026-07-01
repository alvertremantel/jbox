#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class AliasesModel;
class HistoryModel;
class QProcess;

// The single QObject exposed to QML as the `backend` context property.
//
// The QML side calls run(query) (launches in a terminal emulator) or
// runCapture(query) (captures output inline, asynchronously), listens to
// statusChanged for transient feedback, and browses the history/aliases
// models.
class LauncherBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *history READ history CONSTANT)
    Q_PROPERTY(QObject *aliases READ aliases CONSTANT)
    Q_PROPERTY(QString shell READ shell CONSTANT)
    Q_PROPERTY(bool hasTerminal READ hasTerminal CONSTANT)

public:
    LauncherBackend(const QString &aliasesFile, const QString &historyFile, QObject *parent = nullptr);

    QObject *history() const;
    QObject *aliases() const;
    QString shell() const;
    bool hasTerminal() const;

public slots:
    // If `query` matches an alias name, returns the alias's command; else
    // echoes `query` back unchanged.
    QString expand(const QString &query) const;

    // Launches `command` in a terminal emulator window. Falls back to a
    // silent detached background launch if no terminal is available.
    // Fire-and-forget: doesn't wait for the command to finish.
    void run(const QString &command);

    // Runs `command` asynchronously (non-blocking) with a 10s timeout,
    // emitting outputReceived/statusChanged/commandFinished when done. A
    // second call while one is already in flight is ignored.
    void runCapture(const QString &command);

    QVariantList suggestHistory(const QString &prefix) const;
    QVariantList suggestAliases(const QString &prefix) const;

signals:
    void statusChanged(const QString &text);
    void outputReceived(const QString &text);
    void commandStarted(const QString &command);
    void commandFinished(int exitCode);

private:
    void runBackground(const QString &command);

    HistoryModel *m_history;
    AliasesModel *m_aliases;
    QString m_shell;
    bool m_hasTerminal;

    QProcess *m_captureProcess = nullptr;
    bool m_captureTimedOut = false;
};
