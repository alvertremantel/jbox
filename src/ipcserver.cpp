#include "ipcserver.h"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <iostream>

IpcServer::IpcServer(const QString &socketName, QObject *parent)
    : QObject(parent), m_server(new QLocalServer(this)) {
    // Stale socket files from a previous crash are the classic QLocalServer
    // footgun — clear before listening.
    QLocalServer::removeServer(socketName);
    if (!m_server->listen(socketName)) {
        std::cerr << "jbox: failed to start IPC server: " << m_server->errorString().toStdString() << "\n";
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

void IpcServer::onNewConnection() {
    QLocalSocket *sock = m_server->nextPendingConnection();
    if (!sock) {
        return;
    }
    connect(sock, &QLocalSocket::readyRead, this, [this, sock] { drain(sock); });
    connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
}

void IpcServer::drain(QLocalSocket *sock) {
    const QString data = QString::fromUtf8(sock->readAll()).trimmed();
    if (data == QStringLiteral("toggle")) {
        emit toggleRequested();
    } else if (data == QStringLiteral("quit")) {
        QCoreApplication::quit();
    }
}

bool sendToggle(const QString &socketName, const QByteArray &payload, int timeoutMs) {
    QLocalSocket sock;
    sock.connectToServer(socketName);
    if (!sock.waitForConnected(timeoutMs)) {
        return false;
    }
    sock.write(payload);
    sock.flush();
    sock.waitForBytesWritten(timeoutMs);
    sock.disconnectFromServer();
    return true;
}
