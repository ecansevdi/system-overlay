#include <QApplication>
#include <QCommandLineParser>

#include "config/Config.h"
#include "metrics/MetricManager.h"
#include "overlay/OverlayController.h"
#include "sensors/HwmonScanner.h"

#include <cstdio>
#include <memory>

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication): the tray icon uses widgets.
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("system-overlay"));
    QGuiApplication::setApplicationVersion(QStringLiteral(SYSTEM_OVERLAY_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("system-overlay"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Lightweight performance overlay (CPU/GPU/RAM/VRAM/NET) for KDE Plasma.\n"
                       "Uses KWin layer-shell on Wayland so it stays visible above normal,\n"
                       "maximized and fullscreen windows, is click-through and never takes focus."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption intervalOpt(QStringLiteral("interval"),
        QStringLiteral("Refresh interval in milliseconds (100-60000, default 1000)"),
        QStringLiteral("ms"));
    QCommandLineOption screenOpt(QStringLiteral("screen"),
        QStringLiteral("Which screen to show on: primary, all, or an output name (e.g. DP-1)"),
        QStringLiteral("screen"));
    QCommandLineOption debugOpt(QStringLiteral("debug"),
        QStringLiteral("Log sensor and GPU source discovery to stderr"));

    parser.addOptions({intervalOpt, screenOpt, debugOpt});
    parser.process(app);

    Config config;
    config.load();
    if (parser.isSet(intervalOpt)) {
        bool ok = false;
        const int ms = parser.value(intervalOpt).toInt(&ok);
        if (!ok) {
            qCritical("--interval needs a numeric value in milliseconds");
            return 1;
        }
        config.setRefreshInterval(ms);
    }
    if (parser.isSet(screenOpt))
        config.setScreen(parser.value(screenOpt));
    config.setDebug(parser.isSet(debugOpt));

    if (config.debug()) {
        qSetMessagePattern(QStringLiteral("[%{time hh:mm:ss.zzz}] %{type}: %{message}"));
    }

    MetricManager metrics(config);

    if (config.debug()) {
        fprintf(stderr, "system-overlay %s debug info\n", SYSTEM_OVERLAY_VERSION);
        fprintf(stderr, "  session platform: %s\n", qUtf8Printable(QGuiApplication::platformName()));
        fprintf(stderr, "  refresh interval: %d ms\n", config.refreshInterval());
        fprintf(stderr, "hwmon scan:\n%s", qUtf8Printable(HwmonScanner::debugDump()));
        fprintf(stderr, "metric sources:\n%s", qUtf8Printable(metrics.debugInfo()));
        fflush(stderr);
    }

    metrics.prime();

    OverlayController controller(config, &metrics);
    controller.start();

    return app.exec();
}
