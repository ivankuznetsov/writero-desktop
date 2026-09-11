import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// AI side panel: rewrite, research, and image tools with result review.
Drawer {
    id: aiPanel
    objectName: "aiPanel"
    edge: Qt.RightEdge
    width: 400
    height: parent ? parent.height : 600
    modal: false
    interactive: true

    property AiController ai
    property int operationIndex: 0

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
    }

    function run() {
        if (!ai || !providerBox.currentValue)
            return
        const models = modelField.text.trim()
        switch (operationIndex) {
        case 0:
            ai.runRewrite(ai.currentBlock, providerBox.currentValue, models, promptField.text)
            break
        case 1:
            ai.runResearch(ai.currentBlock, providerBox.currentValue, models, promptField.text)
            break
        case 2:
            ai.runImageGeneration(ai.currentBlock, providerBox.currentValue, models,
                                  promptField.text, referenceCheck.checked)
            break
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        Label {
            text: qsTr("AI tools")
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true

            ComboBox {
                id: providerBox
                Layout.fillWidth: true
                textRole: "name"
                valueRole: "id"
                model: aiPanel.ai ? aiPanel.ai.providers.profiles : []
                displayText: currentIndex >= 0 ? currentText : qsTr("No provider configured")
            }
        }

        TextField {
            id: modelField
            Layout.fillWidth: true
            placeholderText: operationIndex === 0
                             ? qsTr("Model or comma-separated models")
                             : qsTr("Model")
            Component.onCompleted: {
                if (aiPanel.ai && providerBox.currentIndex >= 0)
                    text = providerBox.model[providerBox.currentIndex].defaultModel || ""
            }
            Connections {
                target: providerBox
                function onCurrentIndexChanged() {
                    if (providerBox.currentIndex >= 0)
                        modelField.text = providerBox.model[providerBox.currentIndex].defaultModel || ""
                }
            }
        }

        TabBar {
            id: operationBar
            Layout.fillWidth: true
            currentIndex: aiPanel.operationIndex
            onCurrentIndexChanged: aiPanel.operationIndex = currentIndex

            TabButton { text: qsTr("Rewrite") }
            TabButton { text: qsTr("Research") }
            TabButton { text: qsTr("Image") }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120

            TextArea {
                id: promptField
                objectName: "aiPrompt"
                wrapMode: TextEdit.Wrap
                placeholderText: operationIndex === 2
                                 ? qsTr("Describe the image to generate")
                                 : qsTr("Instruction, for example \"make it more concise\"")
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: aiPanel.ai ? aiPanel.ai.suggestions() : []

                Button {
                    required property string modelData
                    text: modelData
                    flat: true
                    font.pixelSize: 12
                    onClicked: promptField.text = modelData
                }
            }
        }

        CheckBox {
            id: referenceCheck
            visible: operationIndex === 2
            text: qsTr("Use current image as reference")
        }

        Button {
            Layout.fillWidth: true
            text: aiPanel.ai && aiPanel.ai.busy ? qsTr("Working\u2026") : qsTr("Run")
            enabled: aiPanel.ai && !aiPanel.ai.busy && providerBox.currentIndex >= 0
            onClicked: aiPanel.run()
        }

        Label {
            Layout.fillWidth: true
            visible: aiPanel.ai && aiPanel.ai.streamingText !== ""
            text: aiPanel.ai ? aiPanel.ai.streamingText : ""
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            color: Theme.textMuted
        }

        Label {
            text: qsTr("Results")
            color: Theme.text
            font.bold: true
        }

        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: aiPanel.ai ? aiPanel.ai.results : []
            ScrollBar.vertical: ScrollBar {}

            delegate: Frame {
                required property var modelData
                width: resultsList.width
                padding: 8

                ColumnLayout {
                    width: parent.width

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: modelData.model + (modelData.stale ? qsTr("  \u00B7 needs review")
                                                                     : "")
                            color: modelData.stale ? Theme.accent : Theme.text
                            font.bold: true
                            elide: Text.ElideMiddle
                        }

                        BusyIndicator {
                            visible: modelData.status === "pending"
                                     || modelData.status === "processing"
                            running: visible
                            width: 18
                            height: 18
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: modelData.status === "completed"
                        text: {
                            if (modelData.kind === "image_generation")
                                return qsTr("[generated image]")
                            const content = modelData.content || ""
                            return content.length > 220 ? content.left(219) + "\u2026" : content
                        }
                        wrapMode: Text.Wrap
                        color: Theme.text
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: modelData.status === "failed"
                        text: modelData.error
                        wrapMode: Text.Wrap
                        color: Theme.danger
                    }

                    RowLayout {
                        visible: modelData.status === "completed"

                        Button {
                            text: qsTr("Replace")
                            onClicked: aiPanel.ai.applyResult(modelData.id, false)
                        }
                        Button {
                            text: qsTr("Insert below")
                            onClicked: aiPanel.ai.applyResult(modelData.id, true)
                        }
                        Button {
                            text: qsTr("Dismiss")
                            flat: true
                            onClicked: aiPanel.ai.dismissResult(modelData.id)
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: resultsList.count === 0
                text: qsTr("No results yet.")
                color: Theme.textFaint
            }
        }
    }
}
