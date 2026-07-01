#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct AliasEntry {
    QString name;
    QString command;
    QString description;
};

// A list of {name, command, description} rows. Persisted to `aliasesFile`
// as JSON on every mutation.
class AliasesModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        CommandRole,
        DescriptionRole,
    };
    Q_ENUM(Role)

    explicit AliasesModel(QString aliasesFile, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the command bound to `name`. A null QString (isNull() ==
    // true) means "no such alias"; a non-null empty string means "alias
    // exists with an empty command" — callers must check isNull(), not
    // isEmpty(), to distinguish the two.
    QString resolve(const QString &name) const;

    // Up to `limit` entries whose name starts with `prefix` (case-insensitive).
    QVector<AliasEntry> search(const QString &prefix, int limit = 8) const;

    Q_INVOKABLE void upsert(const QString &name, const QString &command);
    Q_INVOKABLE void upsert(const QString &name, const QString &command, const QString &description);
    Q_INVOKABLE void remove(const QString &name);

private:
    void save();

    QString m_file;
    QVector<AliasEntry> m_items;
};
