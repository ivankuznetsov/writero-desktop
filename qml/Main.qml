import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import Writero

ApplicationWindow {
    id: window
    width: 1200
    height: 820
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: document.title + "\u2009—\u2009Writero"
    color: Theme.background

    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.background

    Workspace {
        id: workspace
        Component.onCompleted: openDefault()
    }

    DocumentController {
        id: document
        workspace: workspace
    }

    ProviderRegistry {
        id: providers
        workspace: workspace
    }

    AiController {
        id: ai
        workspace: workspace
        providers: providers
        document: document
    }

    AccountSession {
        id: account
        Component.onCompleted: restoreSession()
    }

    Account {
        id: accountDialog
        session: account
    }

    Providers {
        id: providersDialog
        registry: providers
    }

    AiPanel {
        id: aiPanel
        ai: ai
    }

    Connections {
        target: document
        function onLoaded() {
            titleField.text = document.title
            editor.editBlock(Math.max(0, document.blocks.rowCount() - 1), -1)
        }
    }

    Connections {
        target: workspace
        function onOpened() {
            if (!workspace.ready)
                return
            sidebar.refresh()
            if (document.documentId === "")
                document.createDocument()
        }
    }

    Shortcut {
        sequence: "Ctrl+N"
        onActivated: document.createDocument()
    }

    Shortcut {
        sequence: StandardKey.Save
        onActivated: document.saveIfDirty()
    }

    Shortcut {
        sequence: StandardKey.Undo
        onActivated: document.undo()
    }

    Shortcut {
        sequence: StandardKey.Redo
        onActivated: document.redo()
    }

    onClosing: {
        document.saveIfDirty()
        workspace.close()
    }

    header: ToolBar {
        background: Rectangle {
            color: Theme.background
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacing

            Label {
                text: "\u270E"
                color: Theme.accent
                font.pixelSize: 18
                leftPadding: 8
            }

            TextField {
                id: titleField
                objectName: "titleField"
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.contentWidth + 160
                Layout.alignment: Qt.AlignLeft
                placeholderText: qsTr("Untitled")
                selectByMouse: true
                font.pixelSize: 17
                font.weight: Font.DemiBold
                background: Rectangle {
                    color: "transparent"
                }
                onEditingFinished: document.setTitle(text)

                Connections {
                    target: document
                    function onTitleChanged() {
                        if (!titleField.activeFocus)
                            titleField.text = document.title
                    }
                    function onLoaded() {
                        titleField.text = document.title
                    }
                }

                Keys.onEscapePressed: {
                    text = document.title
                    focus = false
                }
            }

            Item {
                Layout.fillWidth: true
            }

            ToolButton {
                text: account.connected ? "\u25C9" : "\u25CB"
                display: AbstractButton.TextOnly
                ToolTip.text: account.connected
                              ? qsTr("Connected as %1").arg(account.accountEmail)
                              : qsTr("Writero account")
                ToolTip.visible: hovered
                onClicked: accountDialog.open()
            }

            ToolButton {
                text: "\u2699"
                display: AbstractButton.TextOnly
                ToolTip.text: qsTr("AI providers")
                ToolTip.visible: hovered
                onClicked: providersDialog.open()
            }

            ToolButton {
                text: "\u22EF"
                display: AbstractButton.TextOnly
                ToolTip.text: qsTr("Document actions")
                ToolTip.visible: hovered
                onClicked: documentMenu.popup()

                Menu {
                    id: documentMenu

                    MenuItem {
                        text: qsTr("Export Markdown\u2026")
                        onTriggered: exportMarkdownDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Export HTML\u2026")
                        onTriggered: exportHtmlDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Export PDF\u2026")
                        onTriggered: exportPdfDialog.open()
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Export bundle\u2026")
                        onTriggered: exportBundleDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Import Markdown\u2026")
                        onTriggered: importMarkdownDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Import bundle\u2026")
                        onTriggered: importBundleDialog.open()
                    }
                }
            }

            ToolButton {
                text: "\u21B6"
                enabled: document.canUndo
                onClicked: document.undo()
                ToolTip.text: qsTr("Undo")
                ToolTip.visible: hovered
            }

            ToolButton {
                text: "\u21B7"
                enabled: document.canRedo
                onClicked: document.redo()
                ToolTip.text: qsTr("Redo")
                ToolTip.visible: hovered
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        DocumentSidebar {
            id: sidebar
            Layout.preferredWidth: 270
            Layout.fillHeight: true
            model: workspace.documents
            activeDocumentId: document.documentId

            onNewDocumentRequested: document.createDocument()
            onDocumentSelected: (documentId) => document.openDocument(documentId)
            onDocumentTrashed: (documentId) => workspace.trashDocument(documentId)
            onDocumentRestored: (documentId) => workspace.restoreDocument(documentId)
            onDocumentDeleted: (documentId) => {
                workspace.deleteDocument(documentId)
                if (document.documentId === documentId)
                    document.createDocument()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background

            BlockEditor {
                id: editor
                anchors.fill: parent
                controller: document

                onAiRequested: (index) => {
                    ai.setCurrentBlock(index)
                    aiPanel.open()
                }
            }
        }
    }

    FileDialog {
        id: exportMarkdownDialog
        title: qsTr("Export Markdown")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [qsTr("Markdown (*.md)")]
        onAccepted: document.exportDocument(selectedFile.toString(), "md")
    }

    FileDialog {
        id: exportHtmlDialog
        title: qsTr("Export HTML")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "html"
        nameFilters: [qsTr("HTML (*.html)")]
        onAccepted: document.exportDocument(selectedFile.toString(), "html")
    }

    FileDialog {
        id: exportPdfDialog
        title: qsTr("Export PDF")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [qsTr("PDF (*.pdf)")]
        onAccepted: document.exportDocument(selectedFile.toString(), "pdf")
    }

    FolderDialog {
        id: exportBundleDialog
        title: qsTr("Export bundle to a folder")
        onAccepted: document.exportBundle(selectedFolder.toString())
    }

    FileDialog {
        id: importMarkdownDialog
        title: qsTr("Import Markdown")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Markdown (*.md *.markdown)"), qsTr("All files (*)")]
        onAccepted: document.importMarkdownFile(selectedFile.toString())
    }

    FolderDialog {
        id: importBundleDialog
        title: qsTr("Choose a bundle folder")
        onAccepted: document.importBundle(selectedFolder.toString())
    }

    footer: ToolBar {
        background: Rectangle {
            color: Theme.background
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: Theme.spacing

            Label {
                text: qsTr("%1 words").arg(document.wordCount)
                color: Theme.textMuted
            }

            Label {
                text: qsTr("%1 characters").arg(document.characterCount)
                color: Theme.textFaint
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                visible: document.saveError !== ""
                text: document.saveError
                color: Theme.danger
            }

            Label {
                visible: document.saveError === ""
                text: document.dirty ? qsTr("Unsaved changes") : qsTr("All changes saved")
                color: document.dirty ? Theme.accent : Theme.textFaint
            }
        }
    }
}
