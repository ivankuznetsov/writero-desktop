import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

ApplicationWindow {
    id: window
    width: 1200
    height: 820
    minimumWidth: 640
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

    DocumentController {
        id: document
        Component.onCompleted: createBlankDocument()
    }

    Shortcut {
        sequence: StandardKey.Undo
        onActivated: document.undo()
    }

    Shortcut {
        sequence: StandardKey.Redo
        onActivated: document.redo()
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
                }

                Component.onCompleted: text = document.title
                Keys.onEscapePressed: {
                    text = document.title
                    focus = false
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

    BlockEditor {
        id: editor
        anchors.fill: parent
        controller: document

        Component.onCompleted: editBlock(0, -1)
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
                text: document.dirty ? qsTr("Unsaved changes") : qsTr("All changes saved")
                color: document.dirty ? Theme.accent : Theme.textFaint
            }
        }
    }
}
