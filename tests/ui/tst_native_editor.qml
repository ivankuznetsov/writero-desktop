import QtQuick
import QtTest

import Writero

Item {
    id: root
    width: 900
    height: 700

    DocumentController {
        id: document
        Component.onCompleted: createBlankDocument()
    }

    BlockEditor {
        id: editor
        anchors.fill: parent
        controller: document
    }

    TestCase {
        name: "NativeEditor"
        when: windowShown

        function init() {
            editor.stopEditing()
            document.createBlankDocument()
        }

        function blockInput() {
            return findChild(editor, "blockInput")
        }

        function typeText(item, text) {
            const alphabet = "abcdefghijklmnopqrstuvwxyz"
            for (let i = 0; i < text.length; ++i) {
                const ch = text.charAt(i)
                const letterIndex = alphabet.indexOf(ch)
                const key = letterIndex >= 0 ? Qt.Key_A + letterIndex : Qt.Key_Space
                keyClick(key)
                waitForRendering(editor)
            }
        }

        function test_blankEditorStartsWithOneBlock() {
            compare(document.blocks.rowCount(), 1, "blank document has one block")
            compare(editor.editingIndex, -1, "no block is being edited initially")
        }

        function test_clickStartsEditingAndTypingUpdatesDocument() {
            const input = blockInput()
            verify(input !== null, "block input exists")

            mouseClick(input)
            compare(editor.editingIndex, 0, "clicking a block starts editing")

            keyClick(Qt.Key_H)
            waitForRendering(editor)
            keyClick(Qt.Key_I)
            waitForRendering(editor)

            compare(document.blocks.get(0).content, "hi",
                    "typing updates the document content")
            verify(document.dirty, "document becomes dirty")
        }

        function test_undoRestoresTypedText() {
            const input = blockInput()
            mouseClick(input)
            keyClick(Qt.Key_X)

            compare(document.blocks.get(0).content, "x", "typed text present")
            document.undo()
            compare(document.blocks.get(0).content, "", "undo removes the typed text")
        }

        function test_shiftEnterSplitsBlock() {
            const input = blockInput()
            mouseClick(input)
            waitForRendering(editor)
            typeText(input, "alpha beta")
            compare(document.blocks.get(0).content, "alpha beta", "text typed")

            keyClick(Qt.Key_Return, Qt.ShiftModifier)
            waitForRendering(editor)

            compare(document.blocks.get(0).content, "alpha beta",
                    "first half keeps the text before the cursor")
            compare(document.blocks.get(1).content, "", "new block is empty")
            compare(editor.editingIndex, 1, "editing moves to the new block")
        }

        function test_slashMenuOpensOnEmptyBlock() {
            const input = blockInput()
            mouseClick(input)
            compare(document.blocks.get(0).content, "", "block is empty")

            keyClick(Qt.Key_Slash)
            const menu = findChild(editor, "slashMenu")
            verify(menu !== null, "slash menu exists")
            verify(menu.opened, "slash menu opens on '/'")
            menu.close()
        }
    }
}
