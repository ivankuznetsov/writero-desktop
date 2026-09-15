#include <QtQuickTest/quicktest.h>
#include <QTemporaryDir>
#include <QQmlContext>
#include "storage/workspace.h"

class UiTestSetup : public QObject
{
    Q_OBJECT
public:
    UiTestSetup()
    {
        // The full shell opens its default workspace. Keep the test away from
        // real documents and the user's credential store.
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", m_data.path().toUtf8());
        qputenv("WRITERO_DISABLE_KEYRING", "1");
    }
    Q_INVOKABLE QString screenshotPath() const { return qEnvironmentVariable("WRITERO_QA_SCREENSHOT"); }
    Q_INVOKABLE QString workspacePath() const { return m_data.path() + "/conflicts"; }
    Q_INVOKABLE bool addConflict(writero::Workspace *workspace, const QString &documentId)
    {
        writero::SyncConflict conflict;
        conflict.id = "qa-conflict";
        conflict.documentId = documentId;
        conflict.kind = "content";
        conflict.localContent = "synthetic local";
        conflict.remoteContent = "synthetic remote";
        conflict.createdAt = QDateTime::currentDateTimeUtc();
        return workspace->store()->saveConflict(conflict);
    }
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty("qaFixture", this);
    }
private:
    QTemporaryDir m_data;
};

QUICK_TEST_MAIN_WITH_SETUP(tst_ui, UiTestSetup)
#include "tst_ui.moc"
