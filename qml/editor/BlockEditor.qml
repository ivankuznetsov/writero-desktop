import QtQuick
import QtQuick.Controls

import Writero

// Scrollable list of blocks with a single active editing target.
Item {
    id: root
    property var controller
    property int editingIndex: -1
    property int focusCursor: -1

    function editBlock(index, cursor) {
        if (!controller)
            return
        const count = controller.blocks.rowCount()
        if (count === 0)
            return
        const target = Math.max(0, Math.min(index, count - 1))
        focusCursor = cursor
        editingIndex = target
        list.currentIndex = target
        list.positionViewAtIndex(target, ListView.Contain)
    }

    function stopEditing() {
        editingIndex = -1
        focusCursor = -1
    }

    function deleteBlock(index) {
        if (editingIndex === index)
            stopEditing()
        controller.removeBlock(index)
    }

    function insertAfter(index) {
        const newIndex = controller.insertBlockAfter(index)
        editBlock(newIndex, 0)
    }

    function splitBlock(index, cursor) {
        const newIndex = controller.splitBlock(index, cursor)
        if (newIndex >= 0)
            editBlock(newIndex, 0)
    }

    function mergeBlockIntoPrevious(index) {
        const merged = controller.mergeWithPrevious(index)
        if (merged >= 0)
            editBlock(merged, -1)
    }

    function moveBlock(from, to) {
        stopEditing()
        controller.moveBlock(from, to)
    }

    function openSlashMenu(index) {
        const item = list.itemAtIndex(index)
        if (!item)
            return
        const position = item.mapToItem(root, 0, item.height)
        slashMenu.x = Math.max(Theme.spacing,
                               Math.min(position.x, root.width - slashMenu.width - Theme.spacing))
        slashMenu.y = Math.min(position.y, Math.max(Theme.spacing,
                                                    root.height - slashMenu.height - Theme.spacing))
        slashMenu.openFor(index)
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        reuseItems: true
        cacheBuffer: 4000
        spacing: Theme.blockSpacing
        topMargin: 24
        bottomMargin: 240
        model: root.controller ? root.controller.blocks : null
        ScrollBar.vertical: ScrollBar {}

        // Delegates reach editor state through the attached `ListView.view`.
        property var controller: root.controller
        property int editingIndex: root.editingIndex
        property int focusCursor: root.focusCursor

        function editBlock(index, cursor) { root.editBlock(index, cursor) }
        function splitBlock(index, cursor) { root.splitBlock(index, cursor) }
        function mergeBlockIntoPrevious(index) { root.mergeBlockIntoPrevious(index) }
        function openSlashMenu(index) { root.openSlashMenu(index) }

        delegate: BlockDelegate {
            onRequestEdit: (index, cursor) => root.editBlock(index, cursor)
            onRequestStopEdit: root.stopEditing()
            onRequestDelete: (index) => root.deleteBlock(index)
            onRequestInsertAfter: (index) => root.insertAfter(index)
            onRequestMove: (from, to) => root.moveBlock(from, to)
            onRequestSlashMenu: (index) => root.openSlashMenu(index)
            onContentEdited: (index, text) => root.controller.setBlockContent(index, text, true)
            onTypeRequested: (index, typeKey, level) => root.controller.setBlockType(index,
                                                                                     typeKey,
                                                                                     level)
            onAttachRequested: (index, source) => root.controller.setBlockMetadataValue(index,
                                                                                        "src",
                                                                                        source)
        }

        onCountChanged: {
            if (editingIndex >= count)
                stopEditing()
        }
    }

    Label {
        anchors.centerIn: parent
        visible: list.count === 0
        text: qsTr("This document is empty.")
        color: Theme.textMuted
    }

    SlashMenu {
        id: slashMenu
        controller: root.controller
        onTypeChosen: (typeKey, headingLevel) => {
            if (slashMenu.blockIndex < 0)
                return
            root.controller.setBlockType(slashMenu.blockIndex, typeKey, headingLevel)
            root.editBlock(slashMenu.blockIndex, 0)
        }
    }
}
