#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>

#include "document/documentsession.h"

namespace writero {

/// List model over a DocumentSession's blocks. Structural session signals are
/// wrapped in begin/end row calls so views animate and recycle correctly.
class BlockListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("BlockListModel is provided by DocumentController.blocks")

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        ContentRole,
        HeadingLevelRole,
        LanguageRole,
        MediaSourceRole,
        MediaAltRole,
        MediaIdRole,
        RevisionRole,
        EmptyRole,
        TextualRole,
    };
    Q_ENUM(Role)

    explicit BlockListModel(QObject *parent = nullptr);

    void setSession(DocumentSession *session);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;

private:
    void attachSession();

    DocumentSession *m_session = nullptr;
};

} // namespace writero
