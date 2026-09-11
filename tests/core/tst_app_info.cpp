#include <QtTest/QtTest>

#include "core/AppInfo.h"

class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void exposesApplicationIdentity()
    {
        QCOMPARE(QString::fromLatin1(writero::app::Name), QStringLiteral("Writero"));
        QCOMPARE(QString::fromLatin1(writero::app::Organization), QStringLiteral("Writero"));
        QVERIFY(writero::app::version() != QStringLiteral("0.0.0"));
    }

    void formatsDisplayVersion()
    {
        QCOMPARE(writero::app::displayVersion(),
                 QStringLiteral("Writero %1").arg(writero::app::version()));
    }
};

QTEST_MAIN(TestAppInfo)
#include "tst_app_info.moc"
