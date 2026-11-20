#include "worker.h"

#include <cerrno>
#include <cstring>

Worker::Worker(QObject* parent) : QObject(parent) {}

Worker::~Worker() {
    bms_.reset();
    serial_.reset();
}

void Worker::registerTypes() {
    qRegisterMetaType<pace::AnalogData>("pace::AnalogData");
    qRegisterMetaType<pace::AlarmData>("pace::AlarmData");
    qRegisterMetaType<pace::AllParams>("pace::AllParams");
}

void Worker::openPort(const QString& port, int baud, int timeoutMs) {
    closePort();

    auto serial = std::make_unique<pace::Serial>();
    if (!serial->open(port.toStdString(), baud, timeoutMs)) {
        int e = errno;
        QString msg = QStringLiteral("Cannot open %1: %2").arg(port, QString::fromLocal8Bit(std::strerror(e)));
        if (e == EACCES || e == EPERM) {
            msg += QStringLiteral(
                "\n\nYour user isn't allowed to use serial ports yet. Run:\n"
                "    sudo usermod -aG dialout $USER\n"
                "then log out and back in.");
        }
        emit openFailed(msg);
        emit requestDone();
        return;
    }

    serial_ = std::move(serial);
    bms_    = std::make_unique<pace::BMS>(*serial_);

    std::string err;
    int packs = bms_->discover(err);
    if (packs <= 0) {
        // Some BMS firmwares don't answer discovery; fall back to probing pack 1.
        pace::AnalogData probe;
        std::string err2;
        if (!bms_->get_analog(1, probe, err2)) {
            QString msg = QStringLiteral("Port opened, but the BMS did not answer.\n\n%1\n\n"
                                         "Check wiring (RS485 A/B), that you're on the RS485 port, and the baud rate.")
                              .arg(QString::fromStdString(err2.empty() ? err : err2));
            bms_.reset();
            serial_.reset();
            emit openFailed(msg);
            emit requestDone();
            return;
        }
        packs = 1;
    }

    std::string version, verr;
    bms_->get_version(1, version, verr);

    emit opened(port, packs, QString::fromStdString(version));
    emit requestDone();
}

void Worker::closePort() {
    bool wasOpen = isOpen();
    bms_.reset();
    serial_.reset();
    if (wasOpen) emit closed();
}

void Worker::poll(int pack) {
    if (!isOpen()) { emit requestDone(); return; }

    pace::AnalogData analog;
    pace::AlarmData  alarm;
    std::string err;
    bool ok = bms_->get_analog(static_cast<uint8_t>(pack), analog, err);
    ok = ok && bms_->get_alarm(static_cast<uint8_t>(pack), alarm, err);

    if (ok) emit liveData(pack, analog, alarm);
    else    emit liveFailed(pack, QString::fromStdString(err));
    emit requestDone();
}

void Worker::loadParams(int pack) {
    if (!isOpen()) { emit requestDone(); return; }

    pace::AllParams p;
    std::string err;
    if (pace::read_all_params(*bms_, static_cast<uint8_t>(pack), p, err))
        emit paramsLoaded(pack, p);
    else
        emit paramsFailed(pack, QString::fromStdString(err));
    emit requestDone();
}

void Worker::saveParams(int pack, pace::AllParams params, pace::DirtyFlags dirty) {
    if (!isOpen()) { emit requestDone(); return; }

    std::string err;
    if (!pace::write_dirty_params(*bms_, params, dirty, err)) {
        emit saveFailed(pack, QString::fromStdString(err));
        emit requestDone();
        return;
    }
    emit paramsSaved(pack);

    // Read everything back so the window shows what the BMS actually stored.
    pace::AllParams readBack;
    std::string rerr;
    if (pace::read_all_params(*bms_, static_cast<uint8_t>(pack), readBack, rerr))
        emit paramsLoaded(pack, readBack);
    else
        emit paramsFailed(pack, QString::fromStdString(rerr));
    emit requestDone();
}
