#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVector>

#include "storage/workspacestore.h"

namespace writero {

/// Document list for the library sidebar. Backed by WorkspaceStore queries so
/// large libraries do not require loading every document.
class DocumentLibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("DocumentLibraryModel is provided by Workspace.documents")

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        UpdatedAtRole,
        CreatedAtRole,
        TrashedRole,
        RevisionRole,
    };
    Q_ENUM(Role)

    explicit DocumentLibraryModel(QObject *parent = nullptr);

    void setStore(WorkspaceStore *store);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void setShowTrashed(bool trashed);
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    WorkspaceStore *m_store = nullptr;
    QVector<DocumentSummary> m_documents;
    QString m_query;
    bool m_showTrashed = false;
};

} // namespace writero
