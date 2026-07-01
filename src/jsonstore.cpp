#include "jsonstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace JsonStore {

bool atomicWrite(const QString &path, const QByteArray &payload) {
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    const QString tmpPath = path + QStringLiteral(".tmp");
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        if (tmp.write(payload) != payload.size()) {
            return false;
        }
    }

    QFile::remove(path);
    return QFile::rename(tmpPath, path);
}

QJsonArray readJsonArray(const QString &path, const QJsonArray &fallback) {
    QFile file(path);
    if (!file.exists()) {
        return fallback;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return fallback;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        const QString corruptPath = path + QStringLiteral(".corrupt");
        QFile::remove(corruptPath);
        QFile::rename(path, corruptPath);
        return fallback;
    }

    return doc.array();
}

}
