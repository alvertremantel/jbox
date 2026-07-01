#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

// Listens on `socketName` in the primary instance and forwards 'toggle'
// messages (from a second `jbox` invocation) as toggleRequested(). A
// 'quit' payload terminates the application.
class IpcServer : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(const QString &socketName, QObject *parent = nullptr);

signals:
    void toggleRequested();

private slots:
    void onNewConnection();

private:
    void drain(QLocalSocket *sock);

    QLocalServer *m_server;
};

// Tries to deliver `payload` to an already-running jbox instance listening
// on `socketName`. Returns true if a primary instance is running and the
// message was delivered (caller should exit); false otherwise (caller
// should become primary).
bool sendToggle(const QString &socketName, const QByteArray &payload = "toggle\n", int timeoutMs = 200);
