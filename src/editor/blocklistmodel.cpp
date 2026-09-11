#include "editor/blocklistmodel.h"

namespace writero {

BlockListModel::BlockListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void BlockListModel::setSession(DocumentSession *session)
{
    if (m_session == session)
        return;

    if (m_session)
        m_session->disconnect(this);

    beginResetModel();
    m_session = session;
    attachSession();
    endResetModel();
}

void BlockListModel::attachSession()
{
    if (!m_session)
        return;

    connect(m_session, &DocumentSession::aboutToInsertBlock, this,
            [this](int row) { beginInsertRows(QModelIndex(), row, row); });
    connect(m_session, &DocumentSession::blockInserted, this,
            [this](int) { endInsertRows(); });

    connect(m_session, &DocumentSession::aboutToRemoveBlock, this,
            [this](int row) { beginRemoveRows(QModelIndex(), row, row); });
    connect(m_session, &DocumentSession::blockRemoved, this,
            [this](int) { endRemoveRows(); });

    connect(m_session, &DocumentSession::aboutToMoveBlock, this,
            [this](int from, int to) {
                const int destination = to > from ? to + 1 : to;
                beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
            });
    connect(m_session, &DocumentSession::blockMoved, this,
            [this](int, int) { endMoveRows(); });

    connect(m_session, &DocumentSession::blockChanged, this, [this](int row) {
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
    });

    connect(m_session, &DocumentSession::aboutToReset, this,
            [this] { beginResetModel(); });
    connect(m_session, &DocumentSession::reset, this,
            [this] { endResetModel(); });
}

void BlockListModel::setMediaResolver(std::function<QString(qint64)> resolver)
{
    m_mediaResolver = std::move(resolver);
    if (!m_session || m_session->document().blocks.isEmpty())
        return;
    emit dataChanged(index(0), index(m_session->document().blocks.size() - 1),
                     {MediaUrlRole});
}

int BlockListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_session)
        return 0;
    return m_session->document().blocks.size();
}

QVariant BlockListModel::data(const QModelIndex &index, int role) const
{
    if (!m_session || !index.isValid() || index.row() < 0
        || index.row() >= m_session->document().blocks.size()) {
        return {};
    }

    const Block &block = m_session->document().blocks.at(index.row());
    switch (role) {
    case IdRole:
        return block.id;
    case TypeRole:
        return blocktype::toKey(block.type);
    case ContentRole:
        return block.content;
    case HeadingLevelRole:
        return blocktype::normalizeHeadingLevel(block.headingLevel());
    case LanguageRole:
        return block.language();
    case MediaSourceRole:
        return block.mediaSource();
    case MediaAltRole:
        return block.mediaAlt();
    case MediaIdRole:
        return block.mediaId;
    case MediaUrlRole:
        return block.mediaId > 0 && m_mediaResolver ? m_mediaResolver(block.mediaId) : QString();
    case RevisionRole:
        return block.revision;
    case EmptyRole:
        return block.isEmpty();
    case TextualRole:
        return blocktype::isTextual(block.type);
    default:
        return {};
    }
}

QHash<int, QByteArray> BlockListModel::roleNames() const
{
    return {
        {IdRole, "blockId"},
        {TypeRole, "blockType"},
        {ContentRole, "content"},
        {HeadingLevelRole, "headingLevel"},
        {LanguageRole, "language"},
        {MediaSourceRole, "mediaSource"},
        {MediaAltRole, "mediaAlt"},
        {MediaIdRole, "mediaId"},
        {MediaUrlRole, "mediaUrl"},
        {RevisionRole, "revision"},
        {EmptyRole, "isEmpty"},
        {TextualRole, "textual"},
    };
}

QVariantMap BlockListModel::get(int row) const
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
