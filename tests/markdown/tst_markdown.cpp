#include <QtTest/QtTest>
#include <QRandomGenerator>

#include "markdown/markdown.h"

using namespace writero;

class TestMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void codeRoundTrip_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("leading-indentation") << QStringLiteral("    print('é雪')");
        QTest::newRow("blank-lines") << QStringLiteral("\n\nvalue\n\n");
        QTest::newRow("empty") << QString();
        QTest::newRow("embedded-fence") << QStringLiteral("before\n```\nafter");
    }

    void codeRoundTrip()
    {
        QFETCH(QString, content);
        const Block original = Block::create(BlockType::Code, content);
        const auto blocks = markdown::parse(markdown::serialize({original}));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().type, BlockType::Code);
        QCOMPARE(blocks.first().content, content);
    }

    void closingFenceMustNotHaveInfoString()
    {
        const auto blocks = markdown::parse(QStringLiteral("```md\n```python\nbody\n```"));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().content, QStringLiteral("```python\nbody"));
    }

    void recognizesTildeAndLongFences()
    {
        const auto blocks = markdown::parse(QStringLiteral("~~~~cpp\nx\n~~~\ny\n~~~~"));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().type, BlockType::Code);
        QCOMPARE(blocks.first().content, QStringLiteral("x\n~~~\ny"));
        QCOMPARE(blocks.first().language(), QStringLiteral("cpp"));
        QVERIFY(markdown::looksLikeMarkdown(QStringLiteral("~~~~cpp\nx\n~~~~")));
    }

    void dividerAfterListIsSeparateBlock()
    {
        const auto blocks = markdown::parse(QStringLiteral("- one\n---\nafter"));
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks.at(1).type, BlockType::Divider);
    }

    void mediaRoundTrip()
    {
        Block media = Block::create(BlockType::Media);
        media.setMediaAlt(QStringLiteral("雪 [cat]"));
        media.setMediaSource(QStringLiteral("https://example.test/my image(1).png"));
        const auto blocks = markdown::parse(markdown::serialize({media}));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first().type, BlockType::Media);
        QCOMPARE(blocks.first().mediaAlt(), media.mediaAlt());
        QCOMPARE(blocks.first().mediaSource(), media.mediaSource());
    }

    void stressCodeRoundTrips()
    {
        QRandomGenerator random(45106);
        const QString alphabet = QStringLiteral("abc雪é\n\t `~[]()#");
        for (int iteration = 0; iteration < 2000; ++iteration) {
            QString content;
            const int length = random.bounded(512);
            for (int j = 0; j < length; ++j)
                content += alphabet.at(random.bounded(int(alphabet.size())));
            const auto blocks = markdown::parse(markdown::serialize({Block::create(BlockType::Code, content)}));
            QCOMPARE(blocks.size(), 1);
            QCOMPARE(blocks.first().type, BlockType::Code);
            QCOMPARE(blocks.first().content, content);
        }
    }

    void goldenTableLinksAndNestedListsRoundTrip()
    {
        const QString original = QStringLiteral(
            "| Name | 値 |\n| --- | --- |\n| [café](https://example.test/a?x=1&y=2) | **雪** |\n\n"
            "- parent\n  1. child\n  2. second\n\n> quoted **text**\n");
        const auto blocks = markdown::parse(original);
        const auto roundTrip = markdown::parse(markdown::serialize(blocks));
        QCOMPARE(roundTrip.size(), blocks.size());
        for (int i = 0; i < blocks.size(); ++i) {
            QCOMPARE(roundTrip.at(i).type, blocks.at(i).type);
            QCOMPARE(roundTrip.at(i).content, blocks.at(i).content);
        }
    }

    void parsesHeadingsAndCode()
    {
        const BlockList blocks = markdown::parse(
            QStringLiteral("# Title\n\nSome text\n\n```python\nprint('x')\n```\n\n##### Too deep"));
        QCOMPARE(blocks.size(), 4);
        QCOMPARE(blocks.at(0).type, BlockType::Heading);
        QCOMPARE(blocks.at(0).headingLevel(), QStringLiteral("h1"));
        QCOMPARE(blocks.at(0).content, QStringLiteral("Title"));
        QCOMPARE(blocks.at(1).type, BlockType::Text);
        QCOMPARE(blocks.at(2).type, BlockType::Code);
        QCOMPARE(blocks.at(2).language(), QStringLiteral("python"));
        QCOMPARE(blocks.at(2).content, QStringLiteral("print('x')"));
        QCOMPARE(blocks.at(3).type, BlockType::Text);
        QCOMPARE(blocks.at(3).content, QStringLiteral("Too deep"));
    }

    void parsesAtomicListsWithNesting()
    {
        const BlockList blocks = markdown::parse(
            QStringLiteral("- one\n- two\n  - nested\n    continuation\n\nparagraph"));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(0).type, BlockType::Ul);
        QCOMPARE(blocks.at(0).content,
                 QStringLiteral("- one\n- two\n  - nested\n    continuation"));
        QCOMPARE(blocks.at(1).type, BlockType::Text);
        QCOMPARE(blocks.at(1).content, QStringLiteral("paragraph"));
    }

    void orderedListStaysOneBlock()
    {
        const BlockList blocks =
            markdown::parse(QStringLiteral("1. first\n2. second\n3. third"));
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.at(0).type, BlockType::Ol);
        QCOMPARE(blocks.at(0).content, QStringLiteral("1. first\n2. second\n3. third"));
    }

    void quoteLinesMergeAndDividersSplit()
    {
        const BlockList blocks = markdown::parse(
            QStringLiteral("> one\n> two\n\n---\n\nafter"));
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks.at(0).type, BlockType::Quote);
        QCOMPARE(blocks.at(0).content, QStringLiteral("one\ntwo"));
        QCOMPARE(blocks.at(1).type, BlockType::Divider);
        QCOMPARE(blocks.at(2).type, BlockType::Text);
    }

    void serializesBlocksToMarkdown()
    {
        Block heading = Block::create(BlockType::Heading, QStringLiteral("Title"));
        heading.setHeadingLevel(QStringLiteral("h2"));
        Block code = Block::create(BlockType::Code, QStringLiteral("x = 1"));
        code.setLanguage(QStringLiteral("python"));

        BlockList blocks = {
            heading,
            code,
            Block::create(BlockType::Quote, QStringLiteral("quoted\nlines")),
            Block::create(BlockType::Ul, QStringLiteral("- item")),
            Block::create(BlockType::Divider),
        };

        const QString markdown = markdown::serialize(blocks);
        QVERIFY(markdown.contains(QStringLiteral("## Title")));
        QVERIFY(markdown.contains(QStringLiteral("```python\nx = 1\n```")));
        QVERIFY(markdown.contains(QStringLiteral("> quoted\n> lines")));
        QVERIFY(markdown.contains(QStringLiteral("- item")));
        QVERIFY(markdown.contains(QStringLiteral("---")));
    }

    void mediaUsesResolver()
    {
        Block media = Block::create(BlockType::Media);
        media.setMediaAlt(QStringLiteral("cat"));
        const BlockList blocks = {media};

        const QString markdown = markdown::serialize(blocks, [](const Block &) {
            return QStringLiteral("media/abc123");
        });
        QCOMPARE(markdown, QStringLiteral("![cat](media/abc123)\n"));
    }

    void looksLikeMarkdownDetectsStructures()
    {
        QVERIFY(markdown::looksLikeMarkdown(QStringLiteral("# heading")));
        QVERIFY(markdown::looksLikeMarkdown(QStringLiteral("- item")));
        QVERIFY(markdown::looksLikeMarkdown(QStringLiteral("**bold**")));
        QVERIFY(!markdown::looksLikeMarkdown(QStringLiteral("plain words only")));
    }

    void roundTripPreservesStructure()
    {
        const QString original = QStringLiteral(
            "# Title\n\nIntro paragraph.\n\n- one\n- two\n\n```bash\necho hi\n```\n\n> quote\n");
        const BlockList first = markdown::parse(original);
        const BlockList second = markdown::parse(markdown::serialize(first));
        QCOMPARE(second.size(), first.size());
        for (int i = 0; i < first.size(); ++i) {
            QCOMPARE(second.at(i).type, first.at(i).type);
            QCOMPARE(second.at(i).content, first.at(i).content);
        }
    }
};

QTEST_MAIN(TestMarkdown)
#include "tst_markdown.moc"
