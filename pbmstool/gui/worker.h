#pragma once
#include "bms.h"
#include "params.h"
#include "serial.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <memory>

Q_DECLARE_METATYPE(pace::AnalogData)
Q_DECLARE_METATYPE(pace::AlarmData)
Q_DECLARE_METATYPE(pace::AllParams)

// Owns the serial port and BMS object and runs every (blocking) request on a
// background thread, so the window never freezes while waiting on the BMS.
// Call its slots via QMetaObject::invokeMethod / queued connections only.
class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(QObject* parent = nullptr);
    ~Worker() override;

    static void registerTypes();

    // Request handlers. Run them on the worker thread (see MainWindow::dispatch).
    void openPort(const QString& port, int baud, int timeoutMs);
    void closePort();
    void poll(int pack);
    void loadParams(int pack);
    void saveParams(int pack, pace::AllParams params, pace::DirtyFlags dirty);

signals:
    void opened(const QString& port, int packCount, const QString& version);
    void openFailed(const QString& error);
    void closed();

    void liveData(int pack, const pace::AnalogData& analog, const pace::AlarmData& alarm);
    void liveFailed(int pack, const QString& error);

    void paramsLoaded(int pack, const pace::AllParams& params);
    void paramsFailed(int pack, const QString& error);
    void paramsSaved(int pack);
    void saveFailed(int pack, const QString& error);

    // Emitted after every request (success or failure) so the UI knows the
    // worker is idle again.
    void requestDone();

private:
    bool isOpen() const { return bms_ != nullptr; }

    std::unique_ptr<pace::Serial> serial_;
    std::unique_ptr<pace::BMS>    bms_;
};
