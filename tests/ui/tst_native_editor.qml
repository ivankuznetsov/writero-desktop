import QtQuick
import QtTest

import Writero

// The tests avoid character keyboard synthesis because the physical keyboard
// layout may not be US. Typing is exercised through the editor's insert path,
// while shortcuts, split, indent, and navigation use real key events.
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
            waitForRendering(editor)
        }

        function currentInput(index) {
            const list = findChild(editor, "blockList")
            verify(list !== null, "block list exists")
            const item = list.itemAtIndex(index)
            verify(item !== null, "block delegate exists at index " + index)
            return item.inputItem
        }

        function startEditingAt(index, cursor) {
            const input = currentInput(index)
            mouseClick(input)
            waitForRendering(editor)
            tryCompare(input, "focus", true)
            if (cursor >= 0)
                input.cursorPosition = cursor
            return input
        }

        function test_blankEditorStartsWithOneBlock() {
            compare(document.blocks.rowCount(), 1, "blank document has one block")
            compare(editor.editingIndex, -1, "no block is being edited initially")
        }

        function test_clickStartsEditingAndEditsReachDocument() {
            const input = startEditingAt(0, -1)
            console.log("DIAG focus input", input.focus, "active", input.activeFocus,
                        "visible", input.visible, "editing", editor.editingIndex)
            compare(editor.editingIndex, 0, "clicking a block starts editing")
            verify(input.focus, "text field owns the focus")

            input.insert(input.cursorPosition, "hi")
            waitForRendering(editor)

            compare(document.blocks.get(0).content, "hi",
                    "edits reach the document content")
            verify(document.dirty, "document becomes dirty")
        }

        function test_undoRestoresTypedText() {
            const input = startEditingAt(0, -1)
            input.insert(input.cursorPosition, "x")
            waitForRendering(editor)

            compare(document.blocks.get(0).content, "x", "typed text present")
            document.undo()
            compare(document.blocks.get(0).content, "", "undo removes the typed text")
        }

        function test_shiftEnterSplitsBlock() {
            document.setBlockContent(0, "alpha beta")
            const input = startEditingAt(0, 5)
            compare(input.cursorPosition, 5, "cursor placed for the split")

            keyClick(Qt.Key_Return, Qt.ShiftModifier)
            waitForRendering(editor)

            compare(document.blocks.rowCount(), 3,
                    "split creates a second block plus trailing placeholder")
            compare(document.blocks.get(0).content, "alpha", "first half kept")
            compare(document.blocks.get(1).content, "beta", "second half moved")
            compare(editor.editingIndex, 1, "editing moves to the new block")
        }

        function test_formatShortcutWrapsSelection() {
            document.setBlockContent(0, "hello")
            const input = startEditingAt(0, 0)
            input.select(0, 5)

            keyClick(Qt.Key_B, Qt.ControlModifier)
            waitForRendering(editor)
            compare(document.blocks.get(0).content, "**hello**",
                    "Ctrl+B wraps the selection in bold markers")
        }

        function test_tabIndentsAndOutdentsListItems() {
            document.setBlockType(0, "ul")
            document.setBlockContent(0, "- item")
            const input = startEditingAt(0, 3)

            keyClick(Qt.Key_Tab)
            waitForRendering(editor)
            compare(document.blocks.get(0).content, "  - item", "Tab indents the list line")

            keyClick(Qt.Key_Backtab)
            waitForRendering(editor)
            compare(document.blocks.get(0).content, "- item", "Shift+Tab outdents again")
        }

        function test_listEnterContinuesMarker() {
            document.setBlockType(0, "ul")
            document.setBlockContent(0, "- item")
            const input = startEditingAt(0, -1)
            input.cursorPosition = input.text.length

            keyClick(Qt.Key_Return)
            waitForRendering(editor)

            compare(document.blocks.get(0).content, "- item\n- ",
                    "Enter continues the bullet marker")
        }

        function test_slashMenuOpensOnEmptyBlock() {
            startEditingAt(0, -1)

            keyClick(Qt.Key_Slash)
            const menu = findChild(editor, "slashMenu")
            verify(menu !== null, "slash menu exists")
            verify(menu.opened, "slash menu opens on '/'")
            menu.close()
        }
    }
}
