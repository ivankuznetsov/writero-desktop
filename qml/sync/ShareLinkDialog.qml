import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// Share links exist only for connected documents. Local documents stay
// private and use Export Markdown instead, so the dialog explains the
// fallback rather than offering a dead end.
Dialog {
    id: shareDialog
    objectName: "shareDialog"
    title: qsTr("Share link")
    modal: true
    focus: true
    width: 480
    anchors.centerIn: Overlay.overlay

    property SyncEngine engine
    property DocumentController document
    property string link
    property string error
    property bool copied: false

    onOpened: {
        link = "";
        error = "";
        copied = false;
        if (engine)
            engine.requestShareLink();
    }

    Connections {
        target: shareDialog.engine

        function onShareLinkReady(shareUrl) {
            shareDialog.link = shareUrl;
            shareDialog.error = "";
        }

        function onShareLinkFailed(message) {
            shareDialog.error = message;
        }
    }

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            visible: shareDialog.link !== ""
            wrapMode: Text.Wrap
            text: qsTr("Anyone with this link can read the published version of this document.")
            color: Theme.textMuted
        }

        RowLayout {
            Layout.fillWidth: true
            visible: shareDialog.link !== ""

            TextField {
                id: linkField
                Layout.fillWidth: true
                readOnly: true
                text: shareDialog.link
                selectByMouse: true
                onActivated: selectAll()
            }

            Button {
                text: qsTr("Copy")
                onClicked: {
                    shareDialog.document.copyToClipboard(shareDialog.link);
                    shareDialog.copied = true;
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: shareDialog.copied
            text: qsTr("Link copied to clipboard.")
            color: Theme.accent
        }

        Label {
            Layout.fillWidth: true
            visible: shareDialog.error !== ""
            wrapMode: Text.Wrap
            text: shareDialog.error
            color: Theme.textMuted
        }

        Label {
            Layout.fillWidth: true
            visible: shareDialog.link === "" && shareDialog.error === ""
            text: qsTr("Creating link\u2026")
            color: Theme.textFaint
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                text: qsTr("Close")
                onClicked: shareDialog.close()
            }
        }
    }
}
