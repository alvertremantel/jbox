#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct HistoryEntry {
    QString command;
    int count = 0;
    qint64 lastUsed = 0;
};

// A list of {command, count, lastUsed} rows, most-recent first, capped at
// `maxSize` entries. Persisted to `historyFile` as JSON on every mutation.
class HistoryModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        CommandRole = Qt::UserRole + 1,
        CountRole,
    };
    Q_ENUM(Role)

    explicit HistoryModel(QString historyFile, int maxSize = 200, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Bumps the count/moves-to-front if `command` already exists, otherwise
    // prepends a new entry. Truncates to maxSize and persists.
    Q_INVOKABLE void add(const QString &command);
    Q_INVOKABLE void remove(const QString &command);

    // Up to `limit` entries whose command starts with `prefix`. Not exposed
    // to QML directly — called from LauncherBackend::suggestHistory.
    QVector<HistoryEntry> search(const QString &prefix, int limit = 8) const;

private:
    void save();

    QString m_file;
    int m_maxSize;
    QVector<HistoryEntry> m_items;
};
