#include <QtTest/QtTest>

#include "document/block.h"
#include "document/documentsession.h"
#include "document/listcontent.h"

using namespace writero;

namespace {

Document makeDocument()
{
    Document document;
    document.id = newId();
    document.title = QStringLiteral("Test document");
    document.blocks = {
        Block::create(BlockType::Text, QStringLiteral("Hello world")),
        Block::create(BlockType::Heading, QStringLiteral("A heading")),
        Block::create(BlockType::Ul, QStringLiteral("- one\n- two")),
        Block::create(BlockType::Code, QStringLiteral("int main() {}")),
        Block::create(BlockType::Text, QString()),
    };
    document.blocks[1].setHeadingLevel(QStringLiteral("h3"));
    return document;
}

} // namespace

class TestDocumentSession : public QObject
{
    Q_OBJECT

private slots:
    void typeKeysRoundTrip()
    {
        for (BlockType type : blocktype::all()) {
            bool ok = false;
            QCOMPARE(blocktype::fromKey(blocktype::toKey(type), &ok), type);
            QVERIFY(ok);
        }
        bool ok = true;
        blocktype::fromKey(QStringLiteral("nonsense"), &ok);
        QVERIFY(!ok);
    }

    void countsWordsAndCharacters()
    {
        Document document = makeDocument();
        QCOMPARE(document.characterCount(), 11 + 9 + 11 + 13);
        // Text, heading, and list blocks count; code does not. List markers
        // count as whitespace-separated tokens, matching the web counter.
        QCOMPARE(document.wordCount(), 2 + 2 + 4);
    }

    void loadsWithoutHistory()
    {
        DocumentSession session;
        session.load(makeDocument());
        QVERIFY(!session.canUndo());
        QVERIFY(!session.canRedo());
        QVERIFY(!session.isDirty());
        QCOMPARE(session.document().title, QStringLiteral("Test document"));
    }

    void insertBlockIsUndoable()
    {
        DocumentSession session;
        session.load(makeDocument());

        Block inserted = Block::create(BlockType::Quote, QStringLiteral("quoted"));
        QVERIFY(session.insertBlock(1, inserted));
        QCOMPARE(session.document().blocks.at(1).id, inserted.id);
        QCOMPARE(session.document().blocks.size(), 6);
        QVERIFY(session.canUndo());

        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.size(), 5);
        QCOMPARE(session.document().blocks.at(1).type, BlockType::Heading);

        QVERIFY(session.redo());
        QCOMPARE(session.document().blocks.size(), 6);
        QCOMPARE(session.document().blocks.at(1).id, inserted.id);
    }

    void removeBlockRestoresAtSameIndex()
    {
        DocumentSession session;
        session.load(makeDocument());
        const QString removedId = session.document().blocks.at(2).id;

        QVERIFY(session.removeBlock(2));
        QCOMPARE(session.document().indexOf(removedId), -1);
        QVERIFY(session.document().hasTrailingEmptyTextBlock());

        QVERIFY(session.undo());
        QCOMPARE(session.document().indexOf(removedId), 2);
    }

    void moveBlockIsUndoable()
    {
        DocumentSession session;
        session.load(makeDocument());
        const QString firstId = session.document().blocks.at(0).id;

        QVERIFY(session.moveBlock(0, 2));
        QCOMPARE(session.document().blocks.at(2).id, firstId);

        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.at(0).id, firstId);
    }

    void contentUpdatesCoalesceIntoOneUndoStep()
    {
        DocumentSession session;
        session.load(makeDocument());

        QVERIFY(session.updateContent(0, QStringLiteral("H"), true));
        QVERIFY(session.updateContent(0, QStringLiteral("He"), true));
        QVERIFY(session.updateContent(0, QStringLiteral("Hey"), true));
        QCOMPARE(session.document().blocks.at(0).content, QStringLiteral("Hey"));

        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.at(0).content, QStringLiteral("Hello world"));
        QVERIFY(!session.canUndo());
    }

    void nonCoalescedUpdatesStaySeparate()
    {
        DocumentSession session;
        session.load(makeDocument());

        QVERIFY(session.updateContent(0, QStringLiteral("one")));
        QVERIFY(session.updateContent(0, QStringLiteral("two")));
        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.at(0).content, QStringLiteral("one"));
    }

    void updateTypeSetsHeadingLevel()
    {
        DocumentSession session;
        session.load(makeDocument());

        QVERIFY(session.updateType(0, BlockType::Heading, QStringLiteral("h1")));
        QCOMPARE(session.document().blocks.at(0).type, BlockType::Heading);
        QCOMPARE(session.document().blocks.at(0).headingLevel(), QStringLiteral("h1"));

        QVERIFY(session.updateType(0, BlockType::Divider));
        QCOMPARE(session.document().blocks.at(0).type, BlockType::Divider);
        QVERIFY(session.document().blocks.at(0).content.isEmpty());
    }

    void splitBlockTrimsAndKeepsListContinuation()
    {
        DocumentSession session;
        session.load(makeDocument());

        QVERIFY(session.splitBlock(0, 5));
        QCOMPARE(session.document().blocks.at(0).content, QStringLiteral("Hello"));
        QCOMPARE(session.document().blocks.at(1).content, QStringLiteral("world"));

        // Splitting an ordered list derives the next number for the new block.
        DocumentSession listSession;
        Document document;
        document.blocks = {Block::create(BlockType::Ol, QStringLiteral("1. one\n2. two"))};
        listSession.load(document);
        QVERIFY(listSession.splitBlock(0, QStringLiteral("1. one\n2. two").indexOf(QStringLiteral("two"))));
        QCOMPARE(listSession.document().blocks.at(1).content, QStringLiteral("3. two"));
    }

    void mergeWithPreviousJoinsContent()
    {
        DocumentSession session;
        session.load(makeDocument());

        QVERIFY(session.mergeWithPrevious(3));
        QCOMPARE(session.document().blocks.at(2).content,
                 QStringLiteral("- one\n- two\nint main() {}"));
        QCOMPARE(session.document().blocks.size(), 4);

        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.size(), 5);
        QCOMPARE(session.document().blocks.at(3).type, BlockType::Code);
    }

    void trailingEmptyBlockIsMaintained()
    {
        DocumentSession session;
        Document document;
        document.blocks = {Block::create(BlockType::Text, QStringLiteral("only"))};
        session.load(document);
        QVERIFY(!session.document().hasTrailingEmptyTextBlock());

        session.ensureTrailingEmptyBlock();
        QVERIFY(session.document().hasTrailingEmptyTextBlock());

        session.updateContent(session.document().blocks.size() - 1, QStringLiteral("filled"), true);
        // Filling the trailing block appends a fresh placeholder.
        QVERIFY(session.document().hasTrailingEmptyTextBlock());
    }

    void replaceAllIsOneUndoStep()
    {
        DocumentSession session;
        session.load(makeDocument());

        BlockList replacement = {Block::create(BlockType::Text, QStringLiteral("fresh"))};
        QVERIFY(session.replaceAll(replacement));
        QCOMPARE(session.document().blocks.size(), 1);

        QVERIFY(session.undo());
        QCOMPARE(session.document().blocks.size(), 5);
    }

    void journalRecordsChanges()
    {
        DocumentSession session;
        session.load(makeDocument());
        QVERIFY(session.journal().isEmpty());

        session.updateContent(0, QStringLiteral("changed"));
        QCOMPARE(session.journal().size(), 1);
        QCOMPARE(session.journal().first().kind, DocumentChange::Kind::UpdateBlock);

        const auto drained = session.drainJournal();
        QCOMPARE(drained.size(), 1);
        QVERIFY(session.journal().isEmpty());
    }

    void listNormalizationDropsLooseBlankLines()
    {
        const QString normalized = listcontent::normalize(
            QStringLiteral("- one\n\nwrapped continuation\n- two"));
        QCOMPARE(normalized, QStringLiteral("- one\nwrapped continuation\n- two"));
    }

    void listNormalizationKeepsBlankBeforeIndentedContinuation()
    {
        const QString normalized = listcontent::normalize(
            QStringLiteral("- one\n\n    indented continuation"));
        QCOMPARE(normalized, QStringLiteral("- one\n\n    indented continuation"));
    }
};

QTEST_MAIN(TestDocumentSession)
#include "tst_document.moc"
