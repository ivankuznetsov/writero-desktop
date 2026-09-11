#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>

#include <cstdio>

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
        &engine, &QQmlApplicationEngine::warnings, &app,
        [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings)
                qWarning().noquote() << warning.toString();
        });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("Writero", "Main");

    if (engine.rootObjects().isEmpty()) {
        QQmlComponent component(&engine, QStringLiteral("Writero"), QStringLiteral("Main"));
        for (const QQmlError &error : component.errors())
            std::fprintf(stderr, "QML error: %s\n", qPrintable(error.toString()));
    }

    return app.exec();
}
