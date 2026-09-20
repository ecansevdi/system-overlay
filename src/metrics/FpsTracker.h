#pragma once

#include <QObject>
#include <QString>

#include <optional>

class FpsDbusAdaptor;

// Counts present-like updates of other windows via a small KWin script
// (Window.damaged), matching Windows ETW "fastest presenting process".
// start()/stop() load and unload that script; sample() is the last reported
// rate, or nullopt if the tracker is not running / has not reported yet.
class FpsTracker : public QObject
{
    Q_OBJECT
public:
    explicit FpsTracker(QObject *parent = nullptr);
    ~FpsTracker() override;

    void start();
    void stop();
    bool running() const { return m_running; }

    std::optional<int> sample() const;

    QString debugInfo() const;

public Q_SLOTS:
    void report(int fps);

private:
    bool writeScriptFile();
    bool loadKwinScript();
    void unloadKwinScript();
    void registerDbus();

    bool m_running = false;
    bool m_dbusReady = false;
    bool m_haveSample = false;
    int m_latest = 0;
    QString m_scriptPath;
    QString m_source;
    FpsDbusAdaptor *m_adaptor = nullptr;
};
