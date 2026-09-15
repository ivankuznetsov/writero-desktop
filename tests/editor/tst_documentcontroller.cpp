#include <QtTest/QtTest>
#include <QTextDocument>
#include <QTextCursor>

#include "document/documentsession.h"
#include "editor/documentcontroller.h"
#include "editor/formatactions.h"

using namespace writero;

class TestDocumentController : public QObject
{
    Q_OBJECT

private slots:
    void blankDocumentHasOneBlock()
    {
        DocumentController controller;
        controller.createBlankDocument();
        QCOMPARE(controller.blocks()->rowCount(), 1);
        QCOMPARE(controller.title(), QStringLiteral("Untitled"));
        QVERIFY(!controller.isDirty());
    }

    void typingUpdatesDocumentAndCounts()
    {
        DocumentController controller;
        controller.createBlankDocument();

        QSignalSpy countsSpy(&controller, &DocumentController::countsChanged);
        controller.setBlockContent(0, QStringLiteral("Hello native world"), true);

        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("Hello native world"));
        QCOMPARE(controller.wordCount(), 3);
        QCOMPARE(controller.characterCount(), 18);
        QVERIFY(controller.isDirty());
        QVERIFY(countsSpy.count() > 0);
    }

    void listEnterContinuesMarker()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ul"));
        controller.setBlockContent(0, QStringLiteral("- first"));

        const int position = controller.handleListEnter(0, QStringLiteral("- first").size());
        QCOMPARE(position, 10); // "- first" + newline + "- "
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("- first\n- "));
    }

    void listEnterOnEmptyItemExitsList()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ol"));
        controller.setBlockContent(0, QStringLiteral("1. one\n2. "));

        const int position = controller.handleListEnter(0, QStringLiteral("1. one\n2. ").size());
        QCOMPARE(position, 7);
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("ol"));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("1. one"));
    }

    void listEnterOnEmptyBlockBecomesText()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ul"));

        controller.handleListEnter(0, 0);
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("text"));
    }

    void orderedListContinuationIncrements()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ol"));
        controller.setBlockContent(0, QStringLiteral("3. three"));

        controller.handleListEnter(0, QStringLiteral("3. three").size());
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("3. three\n4. "));
    }

    void formatWrapsSelection()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("hello world"));

        const QVariantMap result = controller.applyFormat(0, 6, 11, QStringLiteral("bold"));
        QCOMPARE(result.value(QStringLiteral("text")).toString(),
                 QStringLiteral("hello **world**"));
        QCOMPARE(result.value(QStringLiteral("selectionStart")).toInt(), 8);
        QCOMPARE(result.value(QStringLiteral("selectionEnd")).toInt(), 13);
    }

    void formatWithEmptySelectionPlacesCursorInside()
    {
        const formatactions::Selection selection =
            formatactions::wrap(QStringLiteral("ab"), 1, 1, QStringLiteral("italic"));
        QCOMPARE(selection.text, QStringLiteral("a**b"));
        QCOMPARE(selection.cursor, 2);

        const formatactions::Selection bold =
            formatactions::wrap(QStringLiteral("ab"), 1, 1, QStringLiteral("bold"));
        QCOMPARE(bold.text, QStringLiteral("a****b"));
        QCOMPARE(bold.cursor, 3);
    }

    void linkWrapsSelectionWithUrl()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("see docs here"));

        const QVariantMap result =
            controller.applyLink(0, 4, 8, QStringLiteral("https://example.com"));
        QCOMPARE(result.value(QStringLiteral("text")).toString(),
                 QStringLiteral("see [docs](https://example.com) here"));
    }

    void setBlockTypeHeadingStoresLevel()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("Title"));

        controller.setBlockType(0, QStringLiteral("heading"), QStringLiteral("h1"));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("heading"));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("headingLevel")).toString(),
                 QStringLiteral("h1"));
    }

    void insertAndRemoveAreReflectedInModel()
    {
        DocumentController controller;
        controller.createBlankDocument();

        QSignalSpy insertSpy(controller.blocks(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removeSpy(controller.blocks(), &QAbstractItemModel::rowsRemoved);

        const int index = controller.insertBlockAfter(0);
        QCOMPARE(index, 1);
        QCOMPARE(controller.blocks()->rowCount(), 2);
        QCOMPARE(insertSpy.count(), 1);

        controller.removeBlock(1);
        QCOMPARE(controller.blocks()->rowCount(), 1);
        QCOMPARE(removeSpy.count(), 1);
    }

    void splitReturnsNewBlock()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("alpha beta"));

        const int newIndex = controller.splitBlock(0, 5);
        QCOMPARE(newIndex, 1);
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("alpha"));
        QCOMPARE(controller.blocks()->get(1).value(QStringLiteral("content")).toString(),
                 QStringLiteral("beta"));
    }

    void blockMetadataStoresLanguage()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("code"));
        controller.setBlockMetadataValue(0, QStringLiteral("language"), QStringLiteral("python"));

        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("blockType")).toString(),
                 QStringLiteral("code"));
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("language")).toString(),
                 QStringLiteral("python"));

        controller.setBlockMetadataValue(0, QStringLiteral("language"), QString());
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("language")).toString(),
                 QString());
    }

    void indentListItemOnlyForLists()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ul"));
        controller.setBlockContent(0, QStringLiteral("- one\n- two"));

        const int position = controller.indentListItem(0, 8, false);
        QCOMPARE(position, 10);
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("- one\n  - two"));

        const int outdented = controller.indentListItem(0, 10, true);
        QCOMPARE(outdented, 8);
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QStringLiteral("- one\n- two"));

        controller.setBlockType(0, QStringLiteral("text"));
        QCOMPARE(controller.indentListItem(0, 0, false), -1);
    }

    void exitingSoleEmptyListItemRemovesMarker()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ul"));
        controller.setBlockContent(0, QStringLiteral("- "));
        QCOMPARE(controller.handleListEnter(0, 2), 0);
        QCOMPARE(controller.session().document().blocks.at(0).type, BlockType::Text);
        QCOMPARE(controller.session().document().blocks.at(0).content, QString());
        controller.undo();
        QCOMPARE(controller.session().document().blocks.at(0).type, BlockType::Ul);
        QCOMPARE(controller.session().document().blocks.at(0).content, QStringLiteral("- "));
    }

    void outdentAtStartKeepsCursorOnCurrentLine()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ul"));
        controller.setBlockContent(0, QStringLiteral("  - one\n  - two"));
        QCOMPARE(controller.indentListItem(0, 0, true), 0);
        QCOMPARE(controller.indentListItem(0, 6, true), 6);
        QCOMPARE(controller.session().document().blocks.at(0).content,
                 QStringLiteral("- one\n- two"));
    }

    void enterInsideListMarkerDoesNotCorruptMarker()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockType(0, QStringLiteral("ol"));
        controller.setBlockContent(0, QStringLiteral("12. first"));
        QCOMPARE(controller.handleListEnter(0, 1), 9);
        QCOMPARE(controller.session().document().blocks.at(0).content,
                 QStringLiteral("12. \n13. first"));
    }

    void codeFormattingPreservesEmbeddedBackticks_data()
    {
        QTest::addColumn<QString>("selected");
        QTest::newRow("internal") << QStringLiteral("a`b");
        QTest::newRow("ends") << QStringLiteral("`name`");
        QTest::newRow("runs") << QStringLiteral("a``b```c");
        QTest::newRow("edge-spaces") << QStringLiteral(" a ");
    }

    void codeFormattingPreservesEmbeddedBackticks()
    {
        QFETCH(QString, selected);
        const auto result = formatactions::wrap(selected, 0, selected.size(), QStringLiteral("code"));
        QTextDocument rendered;
        rendered.setMarkdown(result.text);
        QCOMPARE(rendered.toPlainText(), selected);
        QCOMPARE(result.text.mid(result.start, result.end - result.start), selected);
    }

    void linkFormattingPreservesLabelAndDestination()
    {
        const QString label = QStringLiteral("a]b");
        const auto result = formatactions::link(label, 0, label.size(),
                                               QStringLiteral("https://example.com/a)b"));
        QTextDocument rendered;
        rendered.setMarkdown(result.text);
        QCOMPARE(rendered.toPlainText(), label);
        QTextCursor cursor(&rendered);
        cursor.setPosition(1);
        const QUrl destination(cursor.charFormat().anchorHref());
        QCOMPARE(destination.scheme(), QStringLiteral("https"));
        QCOMPARE(destination.host(), QStringLiteral("example.com"));
        QCOMPARE(destination.path(), QStringLiteral("/a)b"));
    }

    void linkShortcutKeepsEditableUrlPlaceholder()
    {
        const auto result = formatactions::link(QString(), 0, 0, QStringLiteral("https://"));
        QCOMPARE(result.text, QStringLiteral("[link](https://)"));
    }

    void pasteIdenticalFirstBlockStillInsertsRemainingBlocks()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("alpha"));
        QVERIFY(controller.pasteMarkdown(0, QStringLiteral("alpha\n\nbeta")));
        QCOMPARE(controller.session().document().blocks.at(1).content, QStringLiteral("beta"));
    }

    void multiBlockPasteUndoesAsOneGesture()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("before"), true);
        QVERIFY(controller.pasteMarkdown(0, QStringLiteral("alpha\n\nbeta\n\n# heading")));
        controller.undo();
        QCOMPARE(controller.session().document().blocks.size(), 2);
        QCOMPARE(controller.session().document().blocks.at(0).content, QStringLiteral("before"));
        controller.redo();
        QCOMPARE(controller.session().document().blocks.at(0).content, QStringLiteral("alpha"));
        QCOMPARE(controller.session().document().blocks.at(1).content, QStringLiteral("beta"));
        QCOMPARE(controller.session().document().blocks.at(2).type, BlockType::Heading);
    }

    void pasteUndoRedoStressPreservesWholeDocument()
    {
        for (int iteration = 0; iteration < 100; ++iteration) {
            DocumentController controller;
            controller.createBlankDocument();
            controller.setBlockContent(0, QStringLiteral("draft %1").arg(iteration), true);
            const BlockList before = controller.session().document().blocks;
            QStringList paragraphs;
            for (int index = 0; index < 2 + iteration % 9; ++index)
                paragraphs.append(QStringLiteral("paragraph %1").arg(index));
            QVERIFY(controller.pasteMarkdown(0, paragraphs.join(QStringLiteral("\n\n"))));
            const BlockList pasted = controller.session().document().blocks;
            controller.undo();
            QCOMPARE(controller.session().document().blocks, before);
            controller.redo();
            QCOMPARE(controller.session().document().blocks, pasted);
            controller.setBlockContent(0, QStringLiteral("after paste"), true);
            controller.undo();
            QCOMPARE(controller.session().document().blocks, pasted);
        }
    }

    void undoRestoresContentThroughController()
    {
        DocumentController controller;
        controller.createBlankDocument();
        controller.setBlockContent(0, QStringLiteral("draft"));
        QVERIFY(controller.canUndo());

        controller.undo();
        QCOMPARE(controller.blocks()->get(0).value(QStringLiteral("content")).toString(),
                 QString());
        QVERIFY(controller.canRedo());
    }
};

QTEST_MAIN(TestDocumentController)
#include "tst_documentcontroller.moc"
