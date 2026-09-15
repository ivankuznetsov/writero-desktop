#include <QtTest/QtTest>

#include "ai/textactions.h"

using namespace writero;

class TestTextActions : public QObject
{
    Q_OBJECT

private:
    static Document sampleDocument()
    {
        Document document;
        document.id = newId();
        document.title = QStringLiteral("Essay");
        document.blocks = {
            Block::create(BlockType::Text, QStringLiteral("first paragraph")),
            Block::create(BlockType::Text, QStringLiteral("target paragraph")),
            Block::create(BlockType::Text, QStringLiteral("third paragraph")),
        };
        return document;
    }

private slots:
    void rewriteIncludesContextAndInstruction()
    {
        const Document document = sampleDocument();
        const QVector<AiMessage> messages = textactions::buildMessages(
            textactions::Operation::Rewrite, document, document.blocks.at(1),
            QStringLiteral("make it formal"));

        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.first().role, QStringLiteral("system"));
        QVERIFY(messages.first().content.contains(QStringLiteral("Essay")));
        QVERIFY(messages.first().content.contains(QStringLiteral("first paragraph")));
        QVERIFY(messages.first().content.contains(QStringLiteral("third paragraph")));
        QVERIFY(messages.last().content.contains(QStringLiteral("target paragraph")));
        QVERIFY(messages.last().content.contains(QStringLiteral("make it formal")));
    }

    void humanizeInstructionSwitchesPrompt()
    {
        const Document document = sampleDocument();
        QVERIFY(textactions::isHumanizeRequest(QStringLiteral("please humanize this")));
        QVERIFY(!textactions::isHumanizeRequest(QStringLiteral("translate to French")));

        const QVector<AiMessage> messages = textactions::buildMessages(
            textactions::Operation::Rewrite, document, document.blocks.at(1),
            QStringLiteral("Humanize"));
        QVERIFY(messages.first().content.contains(QStringLiteral("human editor")));
        QVERIFY(messages.last().content.contains(QStringLiteral("Text to humanize")));
        QVERIFY(!messages.last().content.contains(QStringLiteral("Instruction:")));
    }

    void listInstructionPreservesMarkers()
    {
        const Document document = sampleDocument();
        Block list = Block::create(BlockType::Ul, QStringLiteral("- one"));
        const QVector<AiMessage> messages = textactions::buildMessages(
            textactions::Operation::Rewrite, document, list, QStringLiteral("expand"));
        QVERIFY(messages.first().content.contains(QStringLiteral("bullet list")));
    }

    void bulkRewriteRequestsStructuredBlockChanges()
    {
        const auto messages = textactions::buildBulkMessages(textactions::Operation::Rewrite,
            sampleDocument(), QStringLiteral("shorten"));
        QVERIFY(messages.first().content.contains(QStringLiteral("JSON array")));
        QVERIFY(!messages.first().content.contains(QStringLiteral("single block")));
    }

    void humanizeRetainsAdditionalInstructions()
    {
        const auto doc = sampleDocument();
        const auto messages = textactions::buildMessages(textactions::Operation::Rewrite,
            doc, doc.blocks.at(1), QStringLiteral("Humanize and translate to French"));
        QVERIFY(messages.last().content.contains(QStringLiteral("translate to French")));
    }

    void bulkMessagesEncodeBlockIds()
    {
        const Document document = sampleDocument();
        const QVector<AiMessage> messages = textactions::buildBulkMessages(
            textactions::Operation::Polish, document, QStringLiteral("fix typos"));
        QVERIFY(messages.last().content.contains(document.blocks.at(0).id));
        QVERIFY(messages.last().content.contains(QStringLiteral("first paragraph")));
        QVERIFY(messages.first().content.contains(QStringLiteral("JSON array")));
    }
};

QTEST_MAIN(TestTextActions)
#include "tst_textactions.moc"
