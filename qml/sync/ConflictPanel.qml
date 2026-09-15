import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// Review of retained local/remote divergences. Choosing a side creates a new
// version-checked mutation (keep mine) or applies the remote version (take
// theirs); neither choice silently erases the other writer's work.
Dialog {
    id: conflictPanel
    objectName: "conflictPanel"
    title: qsTr("Sync conflicts")
    modal: true
    focus: true
    width: 640
    height: 520
    anchors.centerIn: Overlay.overlay

    property SyncEngine engine

    function refreshConflicts() {
        conflictList.model = engine ? engine.conflictList() : []
    }

    onOpened: refreshConflicts()

    Connections {
        target: conflictPanel.engine
        function onConflictsChanged() { conflictPanel.refreshConflicts() }
        function onChanged() { conflictPanel.refreshConflicts() }
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
            wrapMode: Text.Wrap
            text: qsTr("These blocks changed locally and remotely. Pick the version to keep; "
                       + "the other writer's text stays available in history.")
            color: Theme.textMuted
        }

        ListView {
            id: conflictList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: conflictPanel.engine ? conflictPanel.engine.conflictList() : []
            ScrollBar.vertical: ScrollBar {}

            delegate: Frame {
                required property var modelData
                width: conflictList.width
                padding: 8

                ColumnLayout {
                    width: parent.width

                    Label {
                        text: modelData.kind + "  \u00B7  " + (modelData.blockId || qsTr("document"))
                        color: Theme.accent
                        font.bold: true
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing

                        Label {
                            text: qsTr("Mine")
                            color: Theme.textMuted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.localContent
                            color: Theme.text
                            wrapMode: Text.Wrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }

                        Label {
                            text: qsTr("Theirs")
                            color: Theme.textMuted
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.remoteContent
                            color: Theme.text
                            wrapMode: Text.Wrap
                            maximumLineCount: 4
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight

                        Button {
                            text: qsTr("Keep mine")
                            onClicked: conflictPanel.engine.resolveConflict(modelData.id, true)
                        }
                        Button {
                            text: qsTr("Take theirs")
                            onClicked: conflictPanel.engine.resolveConflict(modelData.id, false)
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: conflictList.count === 0
                text: qsTr("No conflicts.")
                color: Theme.textFaint
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                text: qsTr("Close")
                onClicked: conflictPanel.close()
            }
        }
    }
}
