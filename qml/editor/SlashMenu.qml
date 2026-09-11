import QtQuick
import QtQuick.Controls

import Writero

// Slash-style block type picker. Opened by typing `/` on an empty block or
// from the toolbar. Typing filters the list; Enter inserts the highlighted
// type at the target block.
Popup {
    id: menu
    objectName: "slashMenu"
    property var controller
    property int blockIndex: -1

    signal typeChosen(string typeKey, string headingLevel)

    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var allTypes: [
        { typeKey: "text", level: "", label: qsTr("Text"), keywords: "paragraph body" },
        { typeKey: "heading", level: "h1", label: qsTr("Heading 1"), keywords: "title h1" },
        { typeKey: "heading", level: "h2", label: qsTr("Heading 2"), keywords: "subtitle h2" },
        { typeKey: "heading", level: "h3", label: qsTr("Heading 3"), keywords: "h3" },
        { typeKey: "heading", level: "h4", label: qsTr("Heading 4"), keywords: "h4" },
        { typeKey: "code", level: "", label: qsTr("Code"), keywords: "code fence" },
        { typeKey: "quote", level: "", label: qsTr("Quote"), keywords: "blockquote citation" },
        { typeKey: "ul", level: "", label: qsTr("Bullet List"), keywords: "unordered list" },
        { typeKey: "ol", level: "", label: qsTr("Numbered List"), keywords: "ordered list" },
        { typeKey: "media", level: "", label: qsTr("Image / Video"), keywords: "media picture" },
        { typeKey: "divider", level: "", label: qsTr("Divider"), keywords: "horizontal rule" }
    ]

    readonly property var visibleTypes: {
        const query = filterField.text.toLowerCase()
        if (query === "")
            return allTypes
        return allTypes.filter(entry =>
            entry.label.toLowerCase().includes(query)
            || entry.keywords.includes(query))
    }

    width: 280
    height: 320

    function openFor(index) {
        blockIndex = index
        filterField.text = ""
        if (visibleTypes.length > 0)
            typeList.currentIndex = 0
        open()
        filterField.forceActiveFocus()
    }

    function chooseCurrent() {
        if (typeList.currentIndex < 0 || typeList.currentIndex >= visibleTypes.length)
            return
        const entry = visibleTypes[typeList.currentIndex]
        typeChosen(entry.typeKey, entry.level)
        close()
    }

    onClosed: blockIndex = -1

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    contentItem: Column {
        spacing: 6

        TextField {
            id: filterField
            objectName: "slashFilter"
            width: parent.width
            placeholderText: qsTr("Filter block types\u2026")
            selectByMouse: true

            Keys.onDownPressed: typeList.incrementCurrentIndex()
            Keys.onUpPressed: typeList.decrementCurrentIndex()
            Keys.onReturnPressed: menu.chooseCurrent()
            Keys.onEnterPressed: menu.chooseCurrent()
            Keys.onEscapePressed: menu.close()
        }

        ListView {
            id: typeList
            width: parent.width
            height: 250
            clip: true
            model: menu.visibleTypes
            currentIndex: 0

            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: ListView.view.width
                text: modelData.label
                highlighted: ListView.isCurrentItem
                onClicked: {
                    typeList.currentIndex = index
                    menu.chooseCurrent()
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }
    }
}
