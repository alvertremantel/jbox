#include "historymodel.h"

#include "jsonstore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

HistoryModel::HistoryModel(QString historyFile, int maxSize, QObject *parent)
    : QAbstractListModel(parent), m_file(std::move(historyFile)), m_maxSize(maxSize) {
    const QJsonArray arr = JsonStore::readJsonArray(m_file);
    m_items.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        HistoryEntry entry;
        entry.command = obj.value("command").toString();
        entry.count = obj.value("count").toInt(0);
        entry.lastUsed = static_cast<qint64>(obj.value("last_used").toDouble(0));
        m_items.append(entry);
    }
}

void HistoryModel::save() {
    QJsonArray arr;
    for (const HistoryEntry &item : m_items) {
        QJsonObject obj;
        obj["command"] = item.command;
        obj["count"] = item.count;
        obj["last_used"] = item.lastUsed;
        arr.append(obj);
    }
    JsonStore::atomicWrite(m_file, QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

void HistoryModel::add(const QString &command) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].command == command) {
            beginResetModel();
            HistoryEntry entry = m_items.takeAt(i);
            entry.count += 1;
            entry.lastUsed = now;
            m_items.prepend(entry);
            endResetModel();
            save();
            return;
        }
    }

    beginResetModel();
    HistoryEntry entry;
    entry.command = command;
    entry.count = 1;
    entry.lastUsed = now;
    m_items.prepend(entry);
    if (m_items.size() > m_maxSize) {
        m_items.resize(m_maxSize);
    }
    endResetModel();
    save();
}

void HistoryModel::remove(const QString &command) {
    beginResetModel();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].command == command) {
            m_items.removeAt(i);
        }
    }
    endResetModel();
    save();
}

QVector<HistoryEntry> HistoryModel::search(const QString &prefix, int limit) const {
    const QString trimmed = prefix.trimmed();
    QVector<HistoryEntry> results;
    if (trimmed.isEmpty()) {
        return results;
    }
    for (const HistoryEntry &item : m_items) {
        if (item.command.startsWith(trimmed)) {
            results.append(item);
            if (results.size() >= limit) {
                break;
            }
        }
    }
    return results;
}

int HistoryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const HistoryEntry &item = m_items[index.row()];
    switch (role) {
    case CommandRole:
    case Qt::DisplayRole:
        return item.command;
    case CountRole:
        return item.count;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const {
    return {
        {CommandRole, "command"},
        {CountRole, "count"},
    };
}
