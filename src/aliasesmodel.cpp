#include "aliasesmodel.h"

#include "jsonstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

AliasesModel::AliasesModel(QString aliasesFile, QObject *parent)
    : QAbstractListModel(parent), m_file(std::move(aliasesFile)) {
    const QJsonArray arr = JsonStore::readJsonArray(m_file);
    m_items.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        AliasEntry entry;
        entry.name = obj.value("name").toString();
        entry.command = obj.value("command").toString();
        entry.description = obj.value("description").toString();
        m_items.append(entry);
    }
}

void AliasesModel::save() {
    QJsonArray arr;
    for (const AliasEntry &item : m_items) {
        QJsonObject obj;
        obj["name"] = item.name;
        obj["command"] = item.command;
        obj["description"] = item.description;
        arr.append(obj);
    }
    JsonStore::atomicWrite(m_file, QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString AliasesModel::resolve(const QString &name) const {
    for (const AliasEntry &item : m_items) {
        if (item.name == name) {
            return item.command;
        }
    }
    return QString();
}

QVector<AliasEntry> AliasesModel::search(const QString &prefix, int limit) const {
    const QString needle = prefix.trimmed().toLower();
    QVector<AliasEntry> results;
    if (needle.isEmpty()) {
        return results;
    }
    for (const AliasEntry &item : m_items) {
        if (item.name.toLower().startsWith(needle)) {
            results.append(item);
            if (results.size() >= limit) {
                break;
            }
        }
    }
    return results;
}

void AliasesModel::upsert(const QString &name, const QString &command) {
    upsert(name, command, QString());
}

void AliasesModel::upsert(const QString &name, const QString &command, const QString &description) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    for (AliasEntry &item : m_items) {
        if (item.name == trimmed) {
            beginResetModel();
            item.command = command;
            item.description = description;
            endResetModel();
            save();
            return;
        }
    }

    beginResetModel();
    m_items.append({trimmed, command, description});
    endResetModel();
    save();
}

void AliasesModel::remove(const QString &name) {
    beginResetModel();
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].name == name) {
            m_items.removeAt(i);
        }
    }
    endResetModel();
    save();
}

int AliasesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AliasesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }
    const AliasEntry &item = m_items[index.row()];
    switch (role) {
    case NameRole:
    case Qt::DisplayRole:
        return item.name;
    case CommandRole:
        return item.command;
    case DescriptionRole:
        return item.description;
    default:
        return {};
    }
}

QHash<int, QByteArray> AliasesModel::roleNames() const {
    return {
        {NameRole, "name"},
        {CommandRole, "command"},
        {DescriptionRole, "description"},
    };
}
