import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

import Writero

// One row of the block editor: hover toolbar, editable content, preview.
Item {
    id: delegate
    required property int index
    required property string blockId
    required property string blockType
    required property string content
    required property string headingLevel
    required property string language
    required property string mediaSource
    required property string mediaAlt
    required property string mediaUrl
    required property bool textual

    readonly property bool isEditing: ListView.view && ListView.view.editingIndex === index
    readonly property bool showToolbar: hoverHandler.hovered || isEditing
    readonly property int totalBlocks: ListView.view ? ListView.view.count : 0
    readonly property real contentWidth: Math.min(delegate.width - 96, Theme.contentWidth)
    readonly property real contentX: Math.round((delegate.width - contentWidth) / 2)

    signal requestEdit(int index, int cursor)
    signal requestStopEdit()
    signal requestDelete(int index)
    signal requestInsertAfter(int index)
    signal requestMove(int from, int to)
    signal requestSlashMenu(int index)
    signal contentEdited(int index, string text)
    signal typeRequested(int index, string typeKey, string level)
    signal attachRequested(int index, string source)
    signal languageRequested(int index, string language)

    readonly property alias inputItem: input

    width: ListView.view ? ListView.view.width : 0
    implicitHeight: blockBody.height + Theme.blockPaddingV * 2
    height: implicitHeight
    opacity: ListView.view && ListView.view.dragSourceIndex === index ? 0.5 : 1.0

    onContentChanged: {
        if (delegate.isEditing)
            syncText()
    }
    onIsEditingChanged: {
        if (isEditing)
            input.text = delegate.content
    }

    function beginEditing(cursor) {
        if (!delegate.isEditing)
            return
        if (input.text !== delegate.content)
            input.text = delegate.content
        input.forceActiveFocus()
        input.cursorPosition = cursor >= 0 ? Math.min(cursor, input.text.length)
                                           : input.text.length
    }

    function syncText() {
        if (input.text === delegate.content)
            return
        const cursor = input.cursorPosition
        input.text = delegate.content
        input.cursorPosition = Math.min(cursor, input.text.length)
    }

    function applySelectionResult(result) {
        if (!result || result.text === undefined)
            return
        input.text = result.text
        input.select(result.selectionStart, result.selectionEnd)
        input.cursorPosition = result.cursor
    }

    HoverHandler {
        id: hoverHandler
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: delegate.requestEdit(delegate.index, -1)
    }

    // Hover toolbar: in the left gutter when it fits, floating above the
    // block otherwise so it never covers the text.
    readonly property bool toolbarAbove: (delegate.contentX - 6) < toolbar.width + 4

    Rectangle {
        id: toolbarBackground
        x: toolbar.x - 2
        y: toolbar.y - 2
        width: toolbar.width + 4
        height: toolbar.height + 4
        z: 2
        visible: delegate.showToolbar
        color: Theme.surface
        opacity: 0.92
        radius: Theme.radius
    }

    Row {
        id: toolbar
        y: delegate.toolbarAbove ? -height + 6 : 2
        x: delegate.toolbarAbove ? delegate.contentX
                                 : Math.max(4, delegate.contentX - width - 6)
        spacing: 0
        z: 3
        visible: delegate.showToolbar

        Item {
            id: dragHandle
            width: 22
            height: 28

            Text {
                anchors.centerIn: parent
                text: "\u28FF"
                color: Theme.textFaint
                font.pixelSize: 14
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                cursorShape: Qt.SizeVerCursor

                onPressed: {
                    ListView.view.dragSourceIndex = delegate.index
                    ListView.view.dragTargetIndex = delegate.index
                }

                onPositionChanged: {
                    const point = dragArea.mapToItem(ListView.view, dragArea.mouseX,
                                                     dragArea.mouseY)
                    let target = delegate.index
                    for (let i = 0; i < ListView.view.count; ++i) {
                        const item = ListView.view.itemAtIndex(i)
                        if (!item)
                            continue
                        if (point.y >= item.y && point.y <= item.y + item.height) {
                            target = i
                            break
                        }
                    }
                    ListView.view.dragTargetIndex = target
                }

                onReleased: {
                    const target = ListView.view.dragTargetIndex
                    ListView.view.dragSourceIndex = -1
                    ListView.view.dragTargetIndex = -1
                    if (target >= 0 && target !== delegate.index)
                        delegate.requestMove(delegate.index, target)
                }
            }
        }

        ToolButton {
            width: 30
            height: 28
            text: "\u2261"
            display: AbstractButton.TextOnly
            ToolTip.text: qsTr("Change block type")
            ToolTip.visible: hovered
            onClicked: typeMenu.popup()

            Menu {
                id: typeMenu
                y: parent.height
                z: 10

                Repeater {
                    model: [
                        { typeKey: "text", label: qsTr("Text"), level: "" },
                        { typeKey: "heading", label: qsTr("Heading 1"), level: "h1" },
                        { typeKey: "heading", label: qsTr("Heading 2"), level: "h2" },
                        { typeKey: "heading", label: qsTr("Heading 3"), level: "h3" },
                        { typeKey: "heading", label: qsTr("Heading 4"), level: "h4" },
                        { typeKey: "code", label: qsTr("Code"), level: "" },
                        { typeKey: "quote", label: qsTr("Quote"), level: "" },
                        { typeKey: "ul", label: qsTr("Bullet List"), level: "" },
                        { typeKey: "ol", label: qsTr("Numbered List"), level: "" },
                        { typeKey: "media", label: qsTr("Image / Video"), level: "" },
                        { typeKey: "divider", label: qsTr("Divider"), level: "" }
                    ]
                    MenuItem {
                        required property var modelData
                        text: modelData.label
                        onClicked: {
                            delegate.requestStopEdit()
                            delegate.typeRequested(delegate.index, modelData.typeKey,
                                                   modelData.level)
                        }
                    }
                }
            }
        }

        Row {
            id: formatBar
            spacing: 0
            visible: delegate.isEditing && delegate.textual && delegate.blockType !== "code"

            ToolButton {
                width: 26
                height: 28
                text: "B"
                display: AbstractButton.TextOnly
                font.bold: true
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Bold (Ctrl+B)")
                ToolTip.visible: hovered
                onClicked: delegate.applyFormat("bold")
            }

            ToolButton {
                width: 26
                height: 28
                text: "I"
                display: AbstractButton.TextOnly
                font.italic: true
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Italic (Ctrl+I)")
                ToolTip.visible: hovered
                onClicked: delegate.applyFormat("italic")
            }

            ToolButton {
                width: 26
                height: 28
                text: "</>"
                display: AbstractButton.TextOnly
                font.pixelSize: 11
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Inline code (Ctrl+E)")
                ToolTip.visible: hovered
                onClicked: delegate.applyFormat("code")
            }

            ToolButton {
                width: 26
                height: 28
                text: "S"
                display: AbstractButton.TextOnly
                font.strikeout: true
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Strikethrough")
                ToolTip.visible: hovered
                onClicked: delegate.applyFormat("strikethrough")
            }

            ToolButton {
                width: 26
                height: 28
                text: "\u26AD"
                display: AbstractButton.TextOnly
                focusPolicy: Qt.NoFocus
                ToolTip.text: qsTr("Link")
                ToolTip.visible: hovered
                onClicked: linkDialog.openForSelection()
            }
        }

        ToolButton {
            width: 30
            height: 28
            visible: delegate.blockType === "code"
            text: "{}"
            display: AbstractButton.TextOnly
            focusPolicy: Qt.NoFocus
            ToolTip.text: qsTr("Code language")
            ToolTip.visible: hovered
            onClicked: languageMenu.popup()

            Menu {
                id: languageMenu
                y: parent.height
                z: 10

                Repeater {
                    model: ["plain", "javascript", "typescript", "python", "ruby", "bash",
                            "json", "html", "css", "sql", "cpp", "rust", "go"]
                    MenuItem {
                        required property string modelData
                        text: modelData === "plain" ? qsTr("Plain text") : modelData
                        checkable: true
                        checked: (delegate.language || "plain") === modelData
                        onClicked: delegate.languageRequested(delegate.index,
                                                              modelData === "plain" ? ""
                                                                                    : modelData)
                    }
                }
            }
        }

        ToolButton {
            width: 30
            height: 28
            text: "+"
            display: AbstractButton.TextOnly
            ToolTip.text: qsTr("Add block below")
            ToolTip.visible: hovered
            onClicked: delegate.requestInsertAfter(delegate.index)
        }

        ToolButton {
            width: 30
            height: 28
            text: "\u22EF"
            display: AbstractButton.TextOnly
            ToolTip.text: qsTr("More block actions")
            ToolTip.visible: hovered
            onClicked: moreMenu.popup()

            Menu {
                id: moreMenu
                y: parent.height
                z: 10

                MenuItem {
                    text: qsTr("Move up")
                    enabled: delegate.index > 0
                    onClicked: delegate.requestMove(delegate.index, delegate.index - 1)
                }
                MenuItem {
                    text: qsTr("Move down")
                    enabled: delegate.index < delegate.totalBlocks - 1
                    onClicked: delegate.requestMove(delegate.index, delegate.index + 1)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Delete block")
                    onClicked: delegate.requestDelete(delegate.index)
                }
            }
        }
    }

    Item {
        id: blockBody
        x: delegate.contentX
        y: Theme.blockPaddingV
        width: delegate.contentWidth
        height: Math.max(childrenRect.height, Theme.blockMinHeight)

        Rectangle {
            visible: delegate.blockType === "code" && delegate.textual
            anchors.fill: input
            anchors.margins: -10
            z: -1
            color: Theme.codeBackground
            radius: Theme.radius
        }

        Rectangle {
            visible: delegate.blockType === "quote" && delegate.textual
            x: -14
            y: 2
            width: 3
            height: Math.max(8, input.height - 4)
            color: Theme.accent
            opacity: 0.7
        }

        // Preview of the block's Markdown while it is not being edited.
        // Keeping a separate read-only field avoids Qt's lossy
        // Markdown-to-text round trip on format switches.
        TextEdit {
            id: preview
            objectName: "blockPreview"
            visible: delegate.textual && !delegate.isEditing
            width: parent.width
            padding: 0
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            activeFocusOnPress: false
            text: delegate.content
            textFormat: delegate.blockType === "heading" || delegate.blockType === "code"
                          ? TextEdit.PlainText : TextEdit.MarkdownText
            height: contentHeight
            font.pixelSize: delegate.blockType === "heading"
                             ? Theme.headingPixelSize(delegate.headingLevel)
                             : (delegate.blockType === "code" ? 15 : 17)
            font.bold: delegate.blockType === "heading"
            font.family: delegate.blockType === "code" ? Theme.monoFont : ""
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.background

            onLinkActivated: (link) => Qt.openUrlExternally(link)

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: delegate.requestEdit(delegate.index, -1)
            }
        }

        // Raw Markdown editor, shown only while the block is being edited.
        TextArea {
            id: input
            objectName: "blockInput"
            visible: delegate.textual && delegate.isEditing
            width: parent.width
            padding: 0
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: true
            textFormat: TextEdit.PlainText
            height: Math.max(contentHeight, Theme.blockMinHeight)
            font.pixelSize: delegate.blockType === "heading"
                             ? Theme.headingPixelSize(delegate.headingLevel)
                             : (delegate.blockType === "code" ? 15 : 17)
            font.bold: delegate.blockType === "heading"
            font.family: delegate.blockType === "code" ? Theme.monoFont : ""
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.background
            placeholderText: delegate.index === 0 ? qsTr("Start writing\u2026") : ""
            placeholderTextColor: Theme.textFaint

            onActiveFocusChanged: {
                if (!activeFocus || !delegate.isEditing)
                    return
                const requested = ListView.view ? ListView.view.focusCursor : -1
                cursorPosition = requested >= 0 ? Math.min(requested, text.length)
                                                : text.length
            }

            onTextChanged: {
                if (delegate.isEditing && delegate.index >= 0)
                    delegate.contentEdited(delegate.index, text)
            }

            Keys.onPressed: (event) => delegate.handleKey(event)
        }

        // Media blocks: preview when available, otherwise an attach control.
        Item {
            id: mediaBody
            visible: delegate.blockType === "media"
            width: parent.width
            height: Math.max(mediaImage.height, mediaPlaceholder.height, Theme.blockMinHeight)

            readonly property string resolvedMediaSource:
                delegate.mediaUrl !== "" ? delegate.mediaUrl : delegate.mediaSource

            Image {
                id: mediaImage
                anchors.horizontalCenter: parent.horizontalCenter
                source: mediaBody.resolvedMediaSource
                sourceSize.width: Math.min(parent.width, 640)
                fillMode: Image.PreserveAspectFit
                visible: mediaBody.resolvedMediaSource !== "" && status !== Image.Error
            }

            Rectangle {
                id: mediaPlaceholder
                visible: mediaBody.resolvedMediaSource === "" || mediaImage.status === Image.Error
                width: parent.width
                height: 120
                color: Theme.surface
                border.color: Theme.border
                radius: Theme.radius

                Column {
                    anchors.centerIn: parent
                    spacing: 6

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: delegate.mediaSource === "" ? qsTr("No media attached")
                                                          : qsTr("Media could not be loaded")
                        color: Theme.textMuted
                    }

                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Attach image\u2026")
                        visible: delegate.isEditing
                        onClicked: attachDialog.open()
                    }
                }
            }
        }

        // Divider block.
        Rectangle {
            visible: delegate.blockType === "divider"
            width: parent.width
            height: 1
            y: Theme.blockMinHeight / 2
            color: Theme.border
        }
    }

    FileDialog {
        id: attachDialog
        title: qsTr("Attach image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp)"), qsTr("All files (*)")]
        onAccepted: delegate.attachRequested(delegate.index, selectedFile.toString())
    }

    Popup {
        id: linkDialog
        objectName: "linkDialog"
        modal: true
        focus: true
        width: 320
        x: Math.round((delegate.width - width) / 2)
        y: 8

        function openForSelection() {
            linkField.text = "https://"
            open()
            linkField.forceActiveFocus()
            linkField.selectAll()
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            radius: Theme.radius
        }

        contentItem: Column {
            spacing: 6

            TextField {
                id: linkField
                width: parent.width
                placeholderText: qsTr("https://example.com")
                selectByMouse: true
                Keys.onReturnPressed: linkDialog.accept()
                Keys.onEnterPressed: linkDialog.accept()
                Keys.onEscapePressed: linkDialog.close()
            }

            Row {
                anchors.right: parent.right
                spacing: 6

                Button {
                    text: qsTr("Cancel")
                    onClicked: linkDialog.close()
                }
                Button {
                    text: qsTr("Add link")
                    onClicked: linkDialog.accept()
                }
            }
        }

        function accept() {
            let url = linkField.text.trim()
            if (url === "")
                return
            if (!/^https?:\/\//.test(url))
                url = "https://" + url
            const result = ListView.view.controller.applyLink(delegate.index,
                                                              input.selectionStart,
                                                              input.selectionEnd, url)
            delegate.applySelectionResult(result)
            close()
        }
    }

    function applyFormat(style) {
        const result = ListView.view.controller.applyFormat(delegate.index,
                                                            input.selectionStart,
                                                            input.selectionEnd, style)
        delegate.applySelectionResult(result)
    }

    function handleKey(event) {
        const ctrl = event.modifiers & Qt.ControlModifier
        const shift = event.modifiers & Qt.ShiftModifier

        if (ctrl && event.key === Qt.Key_B) {
            delegate.applyFormat("bold")
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_I) {
            delegate.applyFormat("italic")
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_E) {
            delegate.applyFormat("code")
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            delegate.requestStopEdit()
            event.accepted = true
        } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            const outdent = event.key === Qt.Key_Backtab || shift
            if (delegate.blockType === "ul" || delegate.blockType === "ol") {
                const position = ListView.view.controller.indentListItem(delegate.index,
                                                                         input.cursorPosition,
                                                                         outdent)
                if (position >= 0) {
                    input.cursorPosition = position
                    event.accepted = true
                }
            } else if (!outdent) {
                input.insert(input.cursorPosition,
                             delegate.blockType === "code" ? "    " : "  ")
                event.accepted = true
            }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            handleEnter(shift, event)
        } else if (event.key === Qt.Key_Backspace && input.cursorPosition === 0
                   && input.selectionStart === input.selectionEnd && delegate.index > 0) {
            ListView.view.mergeBlockIntoPrevious(delegate.index)
            event.accepted = true
        } else if (event.key === Qt.Key_Up && input.cursorPosition === 0
                   && delegate.index > 0) {
            ListView.view.editBlock(delegate.index - 1, -1)
            event.accepted = true
        } else if (event.key === Qt.Key_Down
                   && input.cursorPosition === input.text.length
                   && delegate.index < ListView.view.count - 1) {
            ListView.view.editBlock(delegate.index + 1, 0)
            event.accepted = true
        } else if (event.key === Qt.Key_Slash && input.text === ""
                   && delegate.blockType !== "code") {
            delegate.requestSlashMenu(delegate.index)
            event.accepted = true
        }
    }

    function handleEnter(shift, event) {
        if (shift) {
            ListView.view.splitBlock(delegate.index, input.cursorPosition)
            event.accepted = true
            return
        }
        if (delegate.blockType === "ul" || delegate.blockType === "ol") {
            const position = ListView.view.controller.handleListEnter(delegate.index,
                                                                      input.cursorPosition)
            if (position >= 0) {
                input.cursorPosition = position
                event.accepted = true
            }
            return
        }
        if (delegate.blockType === "heading" || delegate.blockType === "quote") {
            ListView.view.editBlock(delegate.index + 1, 0)
            event.accepted = true
        }
    }
}
