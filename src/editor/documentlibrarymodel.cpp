#include "editor/documentlibrarymodel.h"

namespace writero {

DocumentLibraryModel::DocumentLibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DocumentLibraryModel::setStore(WorkspaceStore *store)
{
    beginResetModel();
    m_store = store;
    m_documents.clear();
    if (m_store)
        m_documents = m_store->listDocuments(m_showTrashed, m_query);
    endResetModel();
}

int DocumentLibraryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_documents.size();
}

QVariant DocumentLibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_documents.size())
        return {};

    const DocumentSummary &summary = m_documents.at(index.row());
    switch (role) {
    case IdRole:
        return summary.id;
    case TitleRole:
        return summary.title;
    case PreviewRole:
        return summary.preview;
    case UpdatedAtRole:
        return summary.updatedAt;
    case CreatedAtRole:
        return summary.createdAt;
    case TrashedRole:
        return summary.trashed;
    case RevisionRole:
        return summary.revision;
    default:
        return {};
    }
}

QHash<int, QByteArray> DocumentLibraryModel::roleNames() const
{
    return {
        {IdRole, "documentId"},
        {TitleRole, "title"},
        {PreviewRole, "preview"},
        {UpdatedAtRole, "updatedAt"},
        {CreatedAtRole, "createdAt"},
        {TrashedRole, "trashed"},
        {RevisionRole, "revision"},
    };
}

void DocumentLibraryModel::refresh()
{
    beginResetModel();
    if (m_store)
        m_documents = m_store->listDocuments(m_showTrashed, m_query);
    else
        m_documents.clear();
    endResetModel();
}

void DocumentLibraryModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    refresh();
}

void DocumentLibraryModel::setShowTrashed(bool trashed)
{
    if (m_showTrashed == trashed)
        return;
    m_showTrashed = trashed;
    refresh();
}

QVariantMap DocumentLibraryModel::get(int row) const
{
    const QModelIndex modelIndex = index(row);
    if (!modelIndex.isValid())
        return {};

    QVariantMap result;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        result.insert(QString::fromUtf8(it.value()), data(modelIndex, it.key()));
    return result;
}

} // namespace writero
