import QtQuick
import QtQuick.Controls
import QtTest
import Writero

Item {
    id: root
    width: 1200
    height: 820

    DocumentController { id: document; Component.onCompleted: createBlankDocument() }

    Workspace { id: conflictWorkspace }
    DocumentController { id: conflictDocument; workspace: conflictWorkspace }
    SyncEngine { id: sync; workspace: conflictWorkspace; document: conflictDocument }

    AccountSession { id: account; baseUrl: "http://127.0.0.1:1" }

    TestCase {
        name: "NativeShell"
        when: windowShown

        function component(name) {
            const c = Qt.createComponent("qrc:/qt/qml/Writero/qml/" + name + ".qml")
            compare(c.status, Component.Ready, c.errorString())
            const item = createTemporaryObject(c, root)
            verify(item !== null, c.errorString())
            return item
        }

        function descendants(item, predicate) {
            if (predicate(item)) return item
            const children = item.children || []
            for (let i = 0; i < children.length; i++) {
                const match = descendants(children[i], predicate)
                if (match) return match
            }
            return null
        }

        function test_mainStarts() {
            const app = component("Main")
            tryVerify(() => app.visible)
            verify(findChild(app, "titleField") !== null)
            if (qaFixture.screenshotPath() !== "") {
                waitForRendering(app.contentItem)
                grabImage(app.contentItem).save(qaFixture.screenshotPath())
            }
            app.close()
            app.destroy()
        }

        function test_closePreservesUnsavedDocumentAfterSaveFailure() {
            const app = component("Main")
            const workspace = findChild(app, "workspace")
            const controller = findChild(app, "documentController")
            verify(workspace !== null && controller !== null)
            controller.setBlockContent(0, "Unsaved draft")
            qaFixture.closeStore(workspace)
            app.close()
            verify(app.visible, "failed save must cancel window close")
            verify(controller.dirty)
            app.destroy()
        }

        function test_shareDialogLoads() {
            const dialog = component("sync/ShareLinkDialog")
            dialog.open()
            tryCompare(dialog, "opened", true)
            dialog.close()
            dialog.destroy()
        }

        function test_providerTypeUpdatesDefaultEndpoint() {
            const dialog = component("settings/Providers")
            dialog.open()
            tryCompare(dialog, "opened", true)
            const type = descendants(dialog.contentItem, item => item.valueRole === "key")
            const url = descendants(dialog.contentItem, item => item.placeholderText === "https://api.example.com/v1")
            verify(type !== null && url !== null)
            type.currentIndex = 0
            url.text = "https://openrouter.ai/api/v1"
            type.forceActiveFocus()
            keyClick(Qt.Key_Down)
            compare(type.currentValue, "openai-compatible")
            compare(url.text, "https://api.openai.com/v1")
            keyClick(Qt.Key_Down)
            compare(url.text, "http://localhost:11434")
            url.text = "http://localhost:45105/custom"
            keyClick(Qt.Key_Up)
            compare(url.text, "http://localhost:45105/custom", "custom endpoint preserved")
            dialog.close()
            dialog.destroy()
        }

        function test_accountStatusRecoversAfterFailedSignIn() {
            const dialog = component("settings/Account")
            dialog.session = account
            dialog.open()
            tryCompare(dialog, "opened", true)
            const status = descendants(dialog.contentItem, item => item.text && item.text.startsWith("Connect to use"))
            const connect = descendants(dialog.contentItem, item => item.text === "Connect\u2026")
            verify(status !== null && connect !== null)
            connect.clicked()
            tryVerify(() => account.lastError !== "", 3000)
            tryCompare(account, "busy", false)
            connect.clicked()
            verify(status.text.startsWith("Connect to use"), "retry retains reactive account status")
            tryCompare(account, "busy", false)
            dialog.close()
            dialog.destroy()
        }

        function test_longContentHistoryRenders() {
            const editor = component("editor/BlockEditor")
            editor.width = root.width
            editor.height = root.height
            editor.controller = document
            const list = findChild(editor, "blockList")
            tryVerify(() => list.itemAtIndex(0) !== null)
            const history = findChild(list.itemAtIndex(0), "historyDialog")
            verify(history !== null)
            history.revisions = [{id: "qa-revision", content: "h".repeat(120), event: "update", source: "qa", createdAt: new Date()}]
            history.open()
            tryCompare(history, "opened", true)
            const preview = descendants(history.contentItem, item => item.text === "h".repeat(79) + "\u2026")
            verify(preview !== null, "long history preview is visible")
            history.close()
            editor.destroy()
        }

        function test_conflictPanelUpdatesAfterCloudChange() {
            verify(conflictWorkspace.open(qaFixture.workspacePath()))
            conflictDocument.createDocument()
            const panel = component("sync/ConflictPanel")
            panel.engine = sync
            panel.open()
            tryCompare(panel, "opened", true)
            const list = descendants(panel.contentItem, item => item.itemAtIndex !== undefined)
            compare(list.count, 0)
            verify(qaFixture.addConflict(conflictWorkspace, conflictDocument.documentId))
            sync.conflictsChanged()
            tryCompare(list, "count", 1, 1000)
            panel.close()
            panel.destroy()
            conflictWorkspace.close()
        }

        function test_longAiResultRenders() {
            const panel = component("ai/AiPanel")
            panel.open()
            tryCompare(panel, "opened", true)
            const list = descendants(panel.contentItem, item => item.count !== undefined && item.clip === true && item.itemAtIndex !== undefined)
            verify(list !== null)
            list.model = [{id: "qa-result", status: "completed", model: "synthetic", kind: "text", stale: false, error: "", content: "x".repeat(300)}]
            tryCompare(list, "count", 1)
            tryVerify(() => list.itemAtIndex(0) !== null)
            const preview = descendants(list.itemAtIndex(0), item => item.text === "x".repeat(219) + "\u2026")
            verify(preview !== null, "long AI output is truncated and visible")
            panel.close()
            panel.destroy()
        }
    }
}
