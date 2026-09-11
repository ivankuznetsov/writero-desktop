#include <QtTest/QtTest>

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
