#include "ScanController.h"
#include "SunburstModel.h"

#include <KCrash>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("asahidisksleuth"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Disk Sleuth"));
    QGuiApplication::setOrganizationName(QStringLiteral("asahidisksleuth"));

    KCrash::initialize();

    ads::ScanController controller;
    ads::SunburstModel sunburstModel;
    sunburstModel.setController(&controller);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("sunburstModel"), &sunburstModel);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("asahidisksleuth"), QStringLiteral("Main"));

    return app.exec();
}
