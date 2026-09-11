#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "core/AppInfo.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(writero::app::Name));
    app.setOrganizationName(QString::fromLatin1(writero::app::Organization));
    app.setOrganizationDomain(QString::fromLatin1(writero::app::OrganizationDomain));
    app.setApplicationVersion(writero::app::version());
    app.setDesktopFileName(QString::fromLatin1(writero::app::DesktopFileName));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("Writero", "Main");

    return app.exec();
}
