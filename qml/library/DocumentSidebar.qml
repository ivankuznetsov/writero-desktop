import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// Document library sidebar: search, document list, and trash.
Rectangle {
    id: sidebar
    color: Theme.surface
    border.color: Theme.border

    property var model
    property string activeDocumentId
    property bool showTrashed: false

    signal documentSelected(string documentId)
    signal documentTrashed(string documentId)
    signal documentRestored(string documentId)
    signal documentDeleted(string documentId)
    signal newDocumentRequested()

    function refresh() {
        if (!model)
            return
        model.setShowTrashed(sidebar.showTrashed)
        model.setQuery(searchField.text)
    }

    onShowTrashedChanged: refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            spacing: Theme.spacing

            TextField {
                id: searchField
                objectName: "librarySearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search documents")
                selectByMouse: true
                onTextChanged: if (sidebar.model) sidebar.model.setQuery(text)
            }

            ToolButton {
                text: "+"
                display: AbstractButton.TextOnly
                ToolTip.text: qsTr("New document")
                ToolTip.visible: hovered
                onClicked: sidebar.newDocumentRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        ListView {
            id: documentList
            objectName: "libraryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sidebar.model
            reuseItems: true
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: documentDelegate
                required property int index
                required property string documentId
                required property string title
                required property string preview
                required property var updatedAt
                required property bool trashed

                width: ListView.view.width
                highlighted: documentId === sidebar.activeDocumentId
                onClicked: sidebar.documentSelected(documentId)

                contentItem: ColumnLayout {
                    spacing: 2

                    Label {
                        Layout.fillWidth: true
                        text: documentDelegate.title
                        color: Theme.text
                        elide: Text.ElideRight
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: documentDelegate.preview !== ""
                        text: documentDelegate.preview
                        color: Theme.textMuted
                        elide: Text.ElideRight
                        font.pixelSize: 12
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    visible: documentDelegate.hovered || documentDelegate.highlighted

                    ToolButton {
                        width: 26
                        height: 26
                        text: documentDelegate.trashed ? "\u21B6" : "\u2715"
                        display: AbstractButton.TextOnly
                        ToolTip.text: documentDelegate.trashed ? qsTr("Restore")
                                                               : qsTr("Move to trash")
                        ToolTip.visible: hovered
                        onClicked: {
                            if (documentDelegate.trashed)
                                sidebar.documentRestored(documentDelegate.documentId)
                            else
                                sidebar.documentTrashed(documentDelegate.documentId)
                        }
                    }

                    ToolButton {
                        width: 26
                        height: 26
                        visible: documentDelegate.trashed
                        text: "\u232B"
                        display: AbstractButton.TextOnly
                        ToolTip.text: qsTr("Delete permanently")
                        ToolTip.visible: hovered
                        onClicked: sidebar.documentDeleted(documentDelegate.documentId)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: documentList.count === 0
                text: sidebar.showTrashed ? qsTr("Trash is empty")
                                          : qsTr("No documents yet.\nPress + to start writing.")
                color: Theme.textFaint
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        TabButton {
            Layout.fillWidth: true
            text: qsTr("Trash")
            checkable: true
            checked: sidebar.showTrashed
            onToggled: sidebar.showTrashed = checked
        }
    }
}
