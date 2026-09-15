import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

Dialog {
    id: providersDialog
    objectName: "providersDialog"
    title: qsTr("AI providers")
    modal: true
    focus: true
    width: 560
    height: 560
    anchors.centerIn: Overlay.overlay

    property ProviderRegistry registry

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
            text: providersDialog.registry && providersDialog.registry.keyringAvailable
                  ? qsTr("API keys are stored in the system keyring. Documents never contain them.")
                  : qsTr("The system keyring is unavailable; keys are kept for this session only.")
            color: Theme.textMuted
        }

        ListView {
            id: providerList
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            model: providersDialog.registry ? providersDialog.registry.profiles : []
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: providerDelegate
                required property var modelData
                width: ListView.view.width

                contentItem: ColumnLayout {
                    spacing: 2

                    Label {
                        text: providerDelegate.modelData.name
                              + "  \u00B7  " + providerDelegate.modelData.typeLabel
                        color: Theme.text
                        font.weight: Font.DemiBold
                    }

                    Label {
                        text: providerDelegate.modelData.baseUrl
                              + (providerDelegate.modelData.defaultModel !== ""
                                 ? "  \u00B7  " + providerDelegate.modelData.defaultModel : "")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }

                    Label {
                        visible: !providerDelegate.modelData.hasCredential
                        text: qsTr("No API key stored")
                        color: Theme.accent
                        font.pixelSize: 11
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    ToolButton {
                        text: qsTr("Key\u2026")
                        display: AbstractButton.TextOnly
                        onClicked: {
                            keyField.text = ""
                            keyDialog.providerId = providerDelegate.modelData.id
                            keyDialog.open()
                        }
                    }

                    ToolButton {
                        text: "\u2715"
                        display: AbstractButton.TextOnly
                        ToolTip.text: qsTr("Remove provider")
                        ToolTip.visible: hovered
                        onClicked: providersDialog.registry.removeProvider(
                                       providerDelegate.modelData.id)
                    }
                }
            }
        }

        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Add provider")

            GridLayout {
                anchors.fill: parent
                columns: 2
                columnSpacing: Theme.spacing
                rowSpacing: 4

                Label { text: qsTr("Name") }
                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("My provider")
                }

                Label { text: qsTr("Type") }
                ComboBox {
                    id: typeField
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "key"
                    model: [
                        { key: "openrouter", label: qsTr("OpenRouter"),
                          base: "https://openrouter.ai/api/v1" },
                        { key: "openai-compatible", label: qsTr("OpenAI-compatible"),
                          base: "https://api.openai.com/v1" },
                        { key: "ollama", label: qsTr("Ollama (local)"),
                          base: "http://localhost:11434" }
                    ]
                    onCurrentIndexChanged: {
                        if (currentIndex < 0 || !baseField)
                            return
                        const usesDefault = model.some(entry => entry.base === baseField.text)
                        if (baseField.text === "" || usesDefault)
                            baseField.text = model[currentIndex].base
                    }
                    Component.onCompleted: {
                        if (baseField.text === "" && currentIndex >= 0)
                            baseField.text = model[currentIndex].base
                    }
                }

                Label { text: qsTr("Base URL") }
                TextField {
                    id: baseField
                    Layout.fillWidth: true
                    placeholderText: qsTr("https://api.example.com/v1")
                }

                Label { text: qsTr("Default model") }
                TextField {
                    id: modelField
                    Layout.fillWidth: true
                    placeholderText: qsTr("model-id")
                }

                Label { text: qsTr("API key") }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: typeField.currentValue === "ollama"
                                     ? qsTr("Not required for local models")
                                     : qsTr("sk-\u2026")
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                text: qsTr("Add provider")
                enabled: typeField.currentValue !== ""
                onClicked: {
                    providersDialog.registry.addProvider(
                        nameField.text, typeField.currentValue, baseField.text,
                        modelField.text, apiKeyField.text)
                    nameField.text = ""
                    modelField.text = ""
                    apiKeyField.text = ""
                }
            }

            Button {
                text: qsTr("Close")
                onClicked: providersDialog.close()
            }
        }
    }

    Dialog {
        id: keyDialog
        property string providerId
        title: qsTr("API key")
        modal: true
        focus: true
        width: 360
        anchors.centerIn: Overlay.overlay

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing

            TextField {
                id: keyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("Paste the API key")
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                Button {
                    text: qsTr("Cancel")
                    onClicked: keyDialog.close()
                }
                Button {
                    text: qsTr("Save key")
                    onClicked: {
                        providersDialog.registry.setCredential(keyDialog.providerId, keyField.text)
                        keyDialog.close()
                    }
                }
            }
        }
    }
}
