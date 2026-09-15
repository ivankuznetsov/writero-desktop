import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// AI side panel: rewrite, image generation, and image explanation.
//
// Model choices come from the provider's model catalog, filtered by what the
// operation actually needs. When a catalog is unavailable the panel falls
// back to a typed model name; when no cataloged model can perform the
// operation, the action is disabled with an explanation instead of failing
// silently.
Drawer {
    id: aiPanel
    objectName: "aiPanel"
    edge: Qt.RightEdge
    width: 420
    height: parent ? parent.height : 600
    modal: false
    interactive: true

    property AiController ai
    property int operationIndex: 0
    property var availableModels: []
    property string capabilityMessage: ""

    readonly property var providers: ai ? ai.providers : null
    readonly property string providerId: providerBox.currentValue ? providerBox.currentValue : ""
    readonly property string operation: operationIndex === 0 ? "text"
                                        : operationIndex === 1 ? "generate" : "explain"

    function buildProviders() {
        const entries = []
        if (aiPanel.ai && aiPanel.ai.account && aiPanel.ai.account.hostedAiEnabled)
            entries.push({ id: "writero", name: qsTr("Writero hosted AI (credits)") })
        if (aiPanel.providers) {
            const profiles = aiPanel.providers.profiles
            for (let i = 0; i < profiles.length; ++i)
                entries.push(profiles[i])
        }
        return entries
    }

    function updateProviders() {
        const selected = providerId
        const entries = buildProviders()
        providerBox.model = entries
        const index = entries.findIndex(entry => entry.id === selected)
        providerBox.currentIndex = index >= 0 ? index : (entries.length > 0 ? 0 : -1)
        updateModels()
    }

    function updateModels() {
        const selected = modelBox.currentValue
        capabilityMessage = ""
        if (!providers || providerId === "") {
            availableModels = []
            return
        }
        if (aiPanel.providerId === "writero") {
            const hosted = aiPanel.operation === "generate"
                ? aiPanel.ai.account.hostedImageModels
                : aiPanel.operation === "explain"
                  ? aiPanel.ai.account.hostedExplanationModels
                  : aiPanel.ai.account.hostedModels
            const entries = []
            for (let i = 0; i < hosted.length; ++i)
                entries.push({ id: hosted[i], name: hosted[i] })
            availableModels = entries
            const previousIndex = entries.findIndex(entry => entry.id === selected)
            modelBox.currentIndex = previousIndex >= 0 ? previousIndex : (entries.length > 0 ? 0 : -1)
            if (availableModels.length === 0)
                capabilityMessage = qsTr("No hosted models are available for this operation.")
            referenceCheck.checked = false
            return
        }
        availableModels = providers.modelsFor(providerId, operation)
        const previousIndex = availableModels.findIndex(entry => entry.id === selected)
        modelBox.currentIndex = previousIndex >= 0 ? previousIndex : (availableModels.length > 0 ? 0 : -1)
        if (providers.modelsLoading(providerId)) {
            capabilityMessage = qsTr("Loading models\u2026")
        } else if (!providers.hasModels(providerId)) {
            capabilityMessage = qsTr("Model catalog not loaded for this provider.")
        } else if (availableModels.length === 0) {
            capabilityMessage = operation === "generate"
                ? qsTr("No image-capable model is available for this provider.")
                : operation === "explain"
                  ? qsTr("No vision-capable model is available for this provider.")
                  : qsTr("No text model is available for this provider.")
        }
        referenceCheck.checked = false
    }

    function selectedModel() {
        if (availableModels.length > 0 && modelBox.currentIndex >= 0)
            return availableModels[modelBox.currentIndex].id
        return fallbackModelField.text.trim()
    }

    function selectedModels() {
        const primary = selectedModel()
        if (primary === "" || operationIndex !== 0)
            return primary
        if (compareBox.currentIndex <= 0)
            return primary
        const second = compareBox.currentValue
        return second && second !== primary ? primary + "," + second : primary
    }

    function run() {
        if (!ai || providerId === "")
            return
        const models = selectedModels()
        if (models === "")
            return
        if (operationIndex === 0)
            ai.runRewrite(ai.currentBlock, providerId, models, promptField.text)
        else if (operationIndex === 1)
            ai.runImageGeneration(ai.currentBlock, providerId, models, promptField.text,
                                  referenceCheck.checked)
        else
            ai.runImageExplanation(ai.currentBlock, providerId, models, promptField.text)
    }

    function canRun() {
        if (!ai || ai.busy || providerId === "" || selectedModels() === "")
            return false
        if (providerId === "writero")
            return true
        if (operationIndex === 1)
            return providers.modelSupportsImageGeneration(providerId, selectedModel())
        if (operationIndex === 2)
            return providers.modelSupportsImageInput(providerId, selectedModel())
        return true
    }

    onOperationIndexChanged: updateModels()
    onProvidersChanged: updateProviders()
    Component.onCompleted: updateProviders()

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
    }

    Connections {
        target: aiPanel.providers
        function onChanged() { aiPanel.updateProviders() }
        function onModelsChanged(id) {
            if (id === aiPanel.providerId)
                aiPanel.updateModels()
        }
        function onModelsFailed(id, error) {
            aiPanel.capabilityMessage = error
        }
    }

    Connections {
        target: aiPanel.ai
        function onChanged() { aiPanel.updateProviders() }
    }

    Connections {
        target: aiPanel.ai ? aiPanel.ai.account : null
        function onChanged() { aiPanel.updateProviders() }
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

        ComboBox {
            id: providerBox
            Layout.fillWidth: true
            textRole: "name"
            valueRole: "id"
            model: []
            displayText: currentIndex >= 0 ? currentText : qsTr("No provider configured")
            onCurrentValueChanged: Qt.callLater(aiPanel.updateModels)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            ComboBox {
                id: modelBox
                Layout.fillWidth: true
                visible: aiPanel.availableModels.length > 0
                textRole: "name"
                valueRole: "id"
                model: aiPanel.availableModels
            }

            TextField {
                id: fallbackModelField
                Layout.fillWidth: true
                visible: aiPanel.availableModels.length === 0
                placeholderText: qsTr("Model id")
            }

            ToolButton {
                text: "\u21BB"
                display: AbstractButton.TextOnly
                enabled: aiPanel.providerId !== ""
                         && !aiPanel.providers.modelsLoading(aiPanel.providerId)
                ToolTip.text: qsTr("Refresh model list")
                ToolTip.visible: hovered
                onClicked: aiPanel.providers.refreshModels(aiPanel.providerId)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: aiPanel.providerId === "writero"
            text: aiPanel.ai && aiPanel.ai.account
                  ? qsTr("Uses hosted credits \u00B7 $%1 remaining")
                        .arg(aiPanel.ai.account.remainingCreditUsd.toFixed(2))
                  : ""
            color: Theme.accent
            font.pixelSize: 12
        }

        ComboBox {
            id: compareBox
            Layout.fillWidth: true
            visible: aiPanel.operationIndex === 0 && aiPanel.availableModels.length > 0
            textRole: "name"
            valueRole: "id"
            model: {
                const entries = [{ id: "", name: qsTr("Do not compare") }]
                for (let i = 0; i < aiPanel.availableModels.length; ++i)
                    entries.push(aiPanel.availableModels[i])
                return entries
            }
        }

        TabBar {
            id: operationBar
            Layout.fillWidth: true
            currentIndex: aiPanel.operationIndex
            onCurrentIndexChanged: aiPanel.operationIndex = currentIndex

            TabButton { text: qsTr("Rewrite") }
            TabButton { text: qsTr("Generate") }
            TabButton { text: qsTr("Explain") }
        }

        Label {
            Layout.fillWidth: true
            visible: aiPanel.capabilityMessage !== ""
            text: aiPanel.capabilityMessage
            wrapMode: Text.Wrap
            color: Theme.textMuted
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 110

            TextArea {
                id: promptField
                objectName: "aiPrompt"
                wrapMode: TextEdit.Wrap
                placeholderText: aiPanel.operationIndex === 1
                                 ? qsTr("Describe the image to generate")
                                 : aiPanel.operationIndex === 2
                                   ? qsTr("What should the explanation focus on?")
                                   : qsTr("Instruction, for example \"make it more concise\"")
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            visible: aiPanel.operationIndex === 0

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
            visible: aiPanel.operationIndex === 1
            enabled: aiPanel.providerId === "writero"
                     ? modelBox.currentIndex >= 0
                     : aiPanel.availableModels.length > 0 && modelBox.currentIndex >= 0
                       && aiPanel.providers.modelSupportsReference(aiPanel.providerId,
                                                                   aiPanel.selectedModel())
            text: qsTr("Use current image as reference")
            ToolTip.visible: hovered && !enabled
            ToolTip.text: aiPanel.availableModels.length === 0
                          ? qsTr("Load the model catalog to see if references are supported.")
                          : qsTr("The selected model or provider does not support reference images.")
        }

        Button {
            Layout.fillWidth: true
            text: aiPanel.ai && aiPanel.ai.busy ? qsTr("Working\u2026") : qsTr("Run")
            enabled: aiPanel.canRun()
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
                            return content.length > 220 ? content.slice(0, 219) + "\u2026" : content
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

                    Label {
                        Layout.fillWidth: true
                        visible: modelData.status === "completed"
                                 && modelData.kind === "image_generation"
                                 && (modelData.content || "") === ""
                        text: qsTr("Generated in the browser. Download it from the web app.")
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        visible: modelData.status === "completed"
                                 && !(modelData.kind === "image_generation"
                                      && (modelData.content || "") === "")

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

    onOpened: {
        updateModels()
        if (providers && providerId !== "" && providerId !== "writero"
            && !providers.hasModels(providerId) && !providers.modelsLoading(providerId))
            providers.refreshModels(providerId)
    }
}
