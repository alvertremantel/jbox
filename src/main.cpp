#include "ipcserver.h"
#include "launcherbackend.h"
#include "windowcontroller.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <iostream>

namespace {

const QString kSocketName = QStringLiteral("jbox.singleton");

QString configDir() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.config");
    }
    return base + QStringLiteral("/jbox");
}

QString aliasesPath() { return configDir() + QStringLiteral("/aliases.json"); }
QString historyPath() { return configDir() + QStringLiteral("/history.json"); }

}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("jbox"));
    QCoreApplication::setApplicationName(QStringLiteral("jbox"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A minimal KDE-styled command launcher."));
    parser.addHelpOption();

    QCommandLineOption toggleOpt(
        QStringLiteral("toggle"),
        QStringLiteral("If a primary instance is running, toggle it; otherwise become primary and show."));
    QCommandLineOption noSingleInstanceOpt(
        QStringLiteral("no-single-instance"),
        QStringLiteral("Skip the single-instance check (useful when debugging the QML)."));
    QCommandLineOption printCfgOpt(QStringLiteral("print-cfg"),
                                    QStringLiteral("Print resolved config paths and exit."));
    QCommandLineOption styleOpt(QStringLiteral("style"),
                                 QStringLiteral("QtQuick.Controls style (default: org.kde.desktop)."),
                                 QStringLiteral("style"),
                                 qEnvironmentVariable("QT_QUICK_CONTROLS_STYLE", "org.kde.desktop"));

    parser.addOption(toggleOpt);
    parser.addOption(noSingleInstanceOpt);
    parser.addOption(printCfgOpt);
    parser.addOption(styleOpt);
    parser.process(app);

    if (parser.isSet(printCfgOpt)) {
        std::cout << "config dir: " << configDir().toStdString() << "\n";
        std::cout << "aliases:    " << aliasesPath().toStdString() << "\n";
        std::cout << "history:    " << historyPath().toStdString() << "\n";
        return 0;
    }

    // Make sure the config dir exists before we hand paths to the backend,
    // so the first ever launch doesn't surprise the user with no suggestions.
    QDir().mkpath(configDir());

    // Single-instance: if another jbox is already running, ping it and exit.
    if (!parser.isSet(noSingleInstanceOpt)) {
        if (sendToggle(kSocketName)) {
            return 0;
        }
    }

    // The launcher survives window close; we only exit on explicit `quit`
    // IPC or a fatal error.
    app.setQuitOnLastWindowClosed(false);

    LauncherBackend backend(aliasesPath(), historyPath());

    QQuickStyle::setStyle(parser.value(styleOpt));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.loadFromModule(QStringLiteral("dev.jbox.app"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "jbox: failed to load QML.\n";
        return 1;
    }

    WindowController controller(engine.rootObjects().constFirst());
    IpcServer ipc(kSocketName);
    QObject::connect(&ipc, &IpcServer::toggleRequested, &controller, &WindowController::toggle);

    // If we were launched as a fresh primary (no one to toggle), show the
    // window immediately. This way `jbox` from a keybind "just works" on
    // the very first run, when no daemon is running yet.
    controller.show();

    return app.exec();
}
