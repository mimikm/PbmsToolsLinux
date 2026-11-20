#include "mainwindow.h"
#include "cellbars.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

// Spin boxes inside a scroll area should not change value when the user is
// just scrolling the page with the mouse wheel.
template <typename Base>
class NoWheel : public Base {
public:
    using Base::Base;
protected:
    void wheelEvent(QWheelEvent* e) override {
        if (!this->hasFocus()) e->ignore();
        else Base::wheelEvent(e);
    }
};
using SafeSpinBox       = NoWheel<QSpinBox>;
using SafeDoubleSpinBox = NoWheel<QDoubleSpinBox>;

const QString kGreen = QStringLiteral("#43a047");
const QString kRed   = QStringLiteral("#d32f2f");
const QString kAmber = QStringLiteral("#f59e0b");
const QString kGrey  = QStringLiteral("#9e9e9e");

QString dot(const QString& color, const QString& text) {
    return QStringLiteral("<span style='color:%1'>&#9679;</span>&nbsp;%2").arg(color, text);
}

void setOkLabel(QLabel* l, bool ok) {
    l->setText(ok ? dot(kGreen, QStringLiteral("OK"))
                  : QStringLiteral("<b style='color:%1'>&#9679;&nbsp;TRIGGERED</b>").arg(kRed));
}

void setOnOffLabel(QLabel* l, bool on) {
    l->setText(on ? QStringLiteral("<b style='color:%1'>&#9679;&nbsp;ON</b>").arg(kGreen)
                  : QStringLiteral("<span style='color:%1'>&#9675;&nbsp;OFF</span>").arg(kGrey));
}

// Pace "state" byte: 0 normal, 1 below lower limit, 2 above upper limit.
QString stateByteText(uint8_t v) {
    switch (v) {
        case 0:  return dot(kGreen, QStringLiteral("OK"));
        case 1:  return QStringLiteral("<b style='color:%1'>&#9679;&nbsp;Below limit</b>").arg(kAmber);
        case 2:  return QStringLiteral("<b style='color:%1'>&#9679;&nbsp;Above limit</b>").arg(kRed);
        default: return QStringLiteral("<b style='color:%1'>&#9679;&nbsp;Code 0x%2</b>")
                     .arg(kAmber).arg(static_cast<int>(v), 2, 16, QLatin1Char('0'));
    }
}

QLabel* bigValue() {
    auto* l = new QLabel(QStringLiteral("–"));
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 1.6);
    f.setBold(true);
    l->setFont(f);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QLabel* caption(const QString& text) {
    auto* l = new QLabel(text);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::PlaceholderText));
    l->setPalette(p);
    return l;
}

struct BitDef { const char* name; int byte; uint8_t mask; };

const BitDef kProtections[] = {
    {"Cell over-voltage",       0, 0x01},
    {"Cell under-voltage",      0, 0x02},
    {"Pack over-voltage",       0, 0x04},
    {"Pack under-voltage",      0, 0x08},
    {"Charge over-current",     0, 0x10},
    {"Discharge over-current",  0, 0x20},
    {"Short circuit",           0, 0x40},
    {"Charger over-voltage",    0, 0x80},
    {"Charge over-temp",        1, 0x01},
    {"Charge under-temp",       1, 0x02},
    {"Discharge over-temp",     1, 0x04},
    {"Discharge under-temp",    1, 0x08},
    {"MOSFET over-temp",        1, 0x40},
};

const BitDef kHardware[] = {
    {"Charge MOSFET",    4, 0x01},
    {"Discharge MOSFET", 4, 0x02},
    {"Temp sensor",      4, 0x04},
    {"Cell fault",       4, 0x10},
    {"Sampling",         4, 0x20},
};

constexpr int kTimeoutMs = 2000;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    Worker::registerTypes();
    setWindowTitle(tr("PbmsTools – Pace BMS"));
    resize(1120, 780);

    // Worker thread
    worker_ = new Worker;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &Worker::opened,       this, &MainWindow::onOpened);
    connect(worker_, &Worker::openFailed,   this, &MainWindow::onOpenFailed);
    connect(worker_, &Worker::closed,       this, &MainWindow::onClosed);
    connect(worker_, &Worker::liveData,     this, &MainWindow::onLiveData);
    connect(worker_, &Worker::liveFailed,   this, &MainWindow::onLiveFailed);
    connect(worker_, &Worker::paramsLoaded, this, &MainWindow::onParamsLoaded);
    connect(worker_, &Worker::paramsFailed, this, &MainWindow::onParamsFailed);
    connect(worker_, &Worker::paramsSaved,  this, &MainWindow::onParamsSaved);
    connect(worker_, &Worker::saveFailed,   this, &MainWindow::onSaveFailed);
    connect(worker_, &Worker::requestDone,  this, &MainWindow::onRequestDone);
    thread_.start();

    // Layout
    auto* central = new QWidget;
    auto* v = new QVBoxLayout(central);
    v->addWidget(buildToolbar());

    tabs_ = new QTabWidget;
    tabs_->addTab(buildLiveTab(),   tr("Live"));
    tabs_->addTab(buildAlarmsTab(), tr("Alarms"));
    tabs_->addTab(buildParamsTab(), tr("Parameters"));
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    v->addWidget(tabs_, 1);
    setCentralWidget(central);

    statusLbl_  = new QLabel;
    lastUpdLbl_ = new QLabel;
    statusBar()->addWidget(statusLbl_, 1);
    statusBar()->addPermanentWidget(lastUpdLbl_);

    // Timers
    connect(&pollTimer_, &QTimer::timeout, this, [this] { requestPoll(); });
    ageTimer_.setInterval(1000);
    connect(&ageTimer_, &QTimer::timeout, this, &MainWindow::updateLastUpdateLabel);
    ageTimer_.start();

    // Restore settings
    QSettings s;
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    rescanPorts();
    const QString lastPort = s.value(QStringLiteral("port")).toString();
    if (!lastPort.isEmpty()) portCombo_->setCurrentText(lastPort);
    const int baudIdx = baudCombo_->findText(s.value(QStringLiteral("baud"), 9600).toString());
    if (baudIdx >= 0) baudCombo_->setCurrentIndex(baudIdx);
    intervalSpin_->setValue(s.value(QStringLiteral("interval"), 2).toInt());
    autoRefresh_->setChecked(s.value(QStringLiteral("autoRefresh"), true).toBool());
    pollTimer_.setInterval(intervalSpin_->value() * 1000);

    setConnectedUi(false);
    clearLive();
    showStatus(tr("Not connected. Pick your RS485/RS232 adapter and click Connect."));
}

MainWindow::~MainWindow() {
    pollTimer_.stop();
    // The worker (and with it the serial port) is deleted when the thread finishes.
    thread_.quit();
    thread_.wait();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (dirty_.any()) {
        auto r = QMessageBox::question(this, tr("Unsaved changes"),
            tr("You have parameter changes that were not written to the BMS. Quit anyway?"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Discard) { event->ignore(); return; }
    }
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("baud"), baudCombo_->currentText());
    s.setValue(QStringLiteral("interval"), intervalSpin_->value());
    s.setValue(QStringLiteral("autoRefresh"), autoRefresh_->isChecked());
    event->accept();
}

template <typename F>
void MainWindow::dispatch(F&& fn) {
    ++pending_;
    QMetaObject::invokeMethod(worker_, std::forward<F>(fn), Qt::QueuedConnection);
}

// ── Toolbar ─────────────────────────────────────────────────────────────────

QWidget* MainWindow::buildToolbar() {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);

    h->addWidget(new QLabel(tr("Port:")));
    portCombo_ = new QComboBox;
    portCombo_->setEditable(true);
    portCombo_->setMinimumContentsLength(22);
    portCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    portCombo_->setToolTip(tr("Serial device of your RS485/RS232 adapter. You can also type a path."));
    h->addWidget(portCombo_, 2);

    rescanBtn_ = new QPushButton(tr("Rescan"));
    connect(rescanBtn_, &QPushButton::clicked, this, &MainWindow::rescanPorts);
    h->addWidget(rescanBtn_);

    h->addWidget(new QLabel(tr("Baud:")));
    baudCombo_ = new QComboBox;
    baudCombo_->addItems({QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
                          QStringLiteral("57600"), QStringLiteral("115200")});
    h->addWidget(baudCombo_);

    connectBtn_ = new QPushButton(tr("Connect"));
    connectBtn_->setDefault(true);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    h->addWidget(connectBtn_);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    h->addSpacing(8);
    h->addWidget(sep);
    h->addSpacing(8);

    h->addWidget(new QLabel(tr("Pack:")));
    packCombo_ = new QComboBox;
    packCombo_->addItem(QStringLiteral("1"));
    connect(packCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::onPackChanged);
    h->addWidget(packCombo_);

    h->addSpacing(12);
    autoRefresh_ = new QCheckBox(tr("Auto-refresh every"));
    connect(autoRefresh_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && connected_) { pollTimer_.start(); requestPoll(); }
        else pollTimer_.stop();
    });
    h->addWidget(autoRefresh_);

    intervalSpin_ = new QSpinBox;
    intervalSpin_->setRange(1, 60);
    intervalSpin_->setSuffix(tr(" s"));
    connect(intervalSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int s) {
        pollTimer_.setInterval(s * 1000);
    });
    h->addWidget(intervalSpin_);

    refreshBtn_ = new QPushButton(tr("Refresh now"));
    connect(refreshBtn_, &QPushButton::clicked, this, [this] { requestPoll(true); });
    h->addWidget(refreshBtn_);

    h->addStretch(1);
    return w;
}

void MainWindow::rescanPorts() {
    const QString current = portCombo_->currentText();
    QSignalBlocker block(portCombo_);
    portCombo_->clear();

    const auto filters = QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot;
    QStringList ports;
    QDir dev(QStringLiteral("/dev"));
    for (const QString& n : dev.entryList({QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*")}, filters, QDir::Name))
        ports << QStringLiteral("/dev/") + n;

    QDir byId(QStringLiteral("/dev/serial/by-id"));
    if (byId.exists()) {
        for (const QFileInfo& fi : byId.entryInfoList(filters, QDir::Name)) {
            portCombo_->addItem(fi.absoluteFilePath());
            const QString target = QFileInfo(fi.symLinkTarget()).fileName();
            portCombo_->setItemData(portCombo_->count() - 1,
                                    tr("Stable name for %1 – survives replugging").arg(target),
                                    Qt::ToolTipRole);
        }
    }
    for (int i = ports.size() - 1; i >= 0; --i) portCombo_->insertItem(0, ports[i]);

    if (portCombo_->lineEdit())
        portCombo_->lineEdit()->setPlaceholderText(
            portCombo_->count() ? QString() : tr("No adapters found – plug one in and Rescan"));

    if (!current.isEmpty()) portCombo_->setCurrentText(current);
    else if (portCombo_->count()) portCombo_->setCurrentIndex(0);
}

void MainWindow::toggleConnection() {
    if (connecting_) return;

    if (connected_) {
        if (dirty_.any()) {
            auto r = QMessageBox::question(this, tr("Unsaved changes"),
                tr("Disconnect and discard parameter changes that were not written?"),
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (r != QMessageBox::Discard) return;
        }
        pollTimer_.stop();
        dispatch([w = worker_] { w->closePort(); w->requestDone(); });
        return;
    }

    const QString port = portCombo_->currentText().trimmed();
    if (port.isEmpty()) {
        QMessageBox::information(this, tr("No port"), tr("Choose or type the serial port of your adapter first."));
        return;
    }
    const int baud = baudCombo_->currentText().toInt();

    connecting_ = true;
    connectBtn_->setText(tr("Connecting…"));
    connectBtn_->setEnabled(false);
    portCombo_->setEnabled(false);
    baudCombo_->setEnabled(false);
    rescanBtn_->setEnabled(false);
    showStatus(tr("Opening %1 at %2 baud and looking for packs…").arg(port).arg(baud));

    dispatch([w = worker_, port, baud] { w->openPort(port, baud, kTimeoutMs); });
}

// ── Live tab ────────────────────────────────────────────────────────────────

QWidget* MainWindow::buildLiveTab() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);

    liveBanner_ = new QLabel;
    liveBanner_->setWordWrap(true);
    liveBanner_->setStyleSheet(QStringLiteral(
        "QLabel { background:#fff4e5; color:#7a4b00; border:1px solid #f5c26b; border-radius:4px; padding:6px; }"));
    liveBanner_->hide();
    outer->addWidget(liveBanner_);

    auto* h = new QHBoxLayout;
    outer->addLayout(h, 1);

    // Cells
    auto* cellBox = new QGroupBox(tr("Cell voltages"));
    auto* cellLay = new QVBoxLayout(cellBox);
    cells_ = new CellBarsWidget;
    auto* scroll = new QScrollArea;
    scroll->setWidget(cells_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    cellLay->addWidget(scroll);
    h->addWidget(cellBox, 3);

    // Right column
    auto* right = new QVBoxLayout;
    h->addLayout(right, 2);

    auto* packBox = new QGroupBox(tr("Pack"));
    auto* g = new QGridLayout(packBox);
    packVoltLbl_ = bigValue();
    currentLbl_  = bigValue();
    powerLbl_    = bigValue();
    g->addWidget(caption(tr("Voltage")), 0, 0);
    g->addWidget(caption(tr("Current")), 0, 1);
    g->addWidget(caption(tr("Power")),   0, 2);
    g->addWidget(packVoltLbl_, 1, 0);
    g->addWidget(currentLbl_,  1, 1);
    g->addWidget(powerLbl_,    1, 2);

    socBar_ = new QProgressBar;
    socBar_->setRange(0, 100);
    socBar_->setFormat(tr("SOC %p%"));
    socBar_->setMinimumHeight(26);
    g->addWidget(socBar_, 2, 0, 1, 3);

    auto* f = new QFormLayout;
    remainLbl_ = new QLabel; fullLbl_ = new QLabel; designLbl_ = new QLabel; cyclesLbl_ = new QLabel;
    f->addRow(tr("Remaining:"),     remainLbl_);
    f->addRow(tr("Full capacity:"), fullLbl_);
    f->addRow(tr("Design capacity:"), designLbl_);
    f->addRow(tr("Cycles:"),        cyclesLbl_);
    g->addLayout(f, 3, 0, 1, 3);
    right->addWidget(packBox);

    auto* statBox = new QGroupBox(tr("Cell spread"));
    auto* sf = new QFormLayout(statBox);
    cellMaxLbl_ = new QLabel; cellMinLbl_ = new QLabel; cellDeltaLbl_ = new QLabel; cellAvgLbl_ = new QLabel;
    sf->addRow(tr("Highest:"), cellMaxLbl_);
    sf->addRow(tr("Lowest:"),  cellMinLbl_);
    sf->addRow(tr("Delta:"),   cellDeltaLbl_);
    sf->addRow(tr("Average:"), cellAvgLbl_);
    right->addWidget(statBox);

    auto* tempBox = new QGroupBox(tr("Temperatures"));
    auto* tl = new QVBoxLayout(tempBox);
    tempsLbl_ = new QLabel;
    tempsLbl_->setTextFormat(Qt::RichText);
    tl->addWidget(tempsLbl_);
    right->addWidget(tempBox);
    right->addStretch(1);

    return page;
}

void MainWindow::clearLive() {
    liveValid_ = false;
    cells_->clear();
    for (QLabel* l : {packVoltLbl_, currentLbl_, powerLbl_}) l->setText(QStringLiteral("–"));
    for (QLabel* l : {remainLbl_, fullLbl_, designLbl_, cyclesLbl_,
                      cellMaxLbl_, cellMinLbl_, cellDeltaLbl_, cellAvgLbl_})
        l->setText(QStringLiteral("–"));
    socBar_->setValue(0);
    tempsLbl_->setText(QStringLiteral("–"));
    liveBanner_->hide();
    lastUpdate_ = QDateTime();
    updateLastUpdateLabel();

    for (QLabel* l : {chgMosLbl_, dchgMosLbl_, balLbl_}) l->setText(QStringLiteral("–"));
    for (QLabel* l : protLbls_) l->setText(QStringLiteral("–"));
    for (QLabel* l : hwLbls_)   l->setText(QStringLiteral("–"));
    cellAlarmsLbl_->setText(QStringLiteral("–"));
    tempAlarmsLbl_->setText(QStringLiteral("–"));
}

void MainWindow::updateLive() {
    const auto& a = analog_;
    cells_->setData(a, alarm_);

    const double v = a.total_volt_mv / 1000.0;
    const double i = a.current_10ma / 100.0;
    packVoltLbl_->setText(QString::number(v, 'f', 2) + QStringLiteral(" V"));

    QString dir;
    QString col;
    if (i > 0.05)       { dir = tr("charging");    col = kGreen; }
    else if (i < -0.05) { dir = tr("discharging"); col = kAmber; }
    currentLbl_->setText(QStringLiteral("%1%2 A").arg(i > 0 ? QStringLiteral("+") : QString())
                                                  .arg(QString::number(i, 'f', 2)));
    currentLbl_->setToolTip(dir);
    currentLbl_->setStyleSheet(col.isEmpty() ? QString() : QStringLiteral("color:%1").arg(col));
    powerLbl_->setText(QString::number(v * i, 'f', 0) + QStringLiteral(" W"));

    const int soc = (a.full_cap_mah > 0)
        ? std::clamp(static_cast<int>(std::lround(a.remain_cap_mah * 100.0 / a.full_cap_mah)), 0, 100) : 0;
    socBar_->setValue(soc);
    remainLbl_->setText(QString::number(a.remain_cap_mah / 1000.0, 'f', 2) + QStringLiteral(" Ah"));
    fullLbl_->setText(QString::number(a.full_cap_mah / 1000.0, 'f', 2) + QStringLiteral(" Ah"));
    designLbl_->setText(a.design_cap_mah > 0
        ? QString::number(a.design_cap_mah / 1000.0, 'f', 2) + QStringLiteral(" Ah") : QStringLiteral("–"));
    cyclesLbl_->setText(QString::number(a.cycle));

    const int n = std::min<int>(a.cell_count, static_cast<int>(a.cell_mv.size()));
    if (n > 0) {
        auto mm = std::minmax_element(a.cell_mv.begin(), a.cell_mv.begin() + n);
        const int iMin = static_cast<int>(mm.first  - a.cell_mv.begin());
        const int iMax = static_cast<int>(mm.second - a.cell_mv.begin());
        const double avg = std::accumulate(a.cell_mv.begin(), a.cell_mv.begin() + n, 0.0) / n;
        cellMaxLbl_->setText(tr("%1 V (cell %2)").arg(*mm.second / 1000.0, 0, 'f', 3).arg(iMax + 1));
        cellMinLbl_->setText(tr("%1 V (cell %2)").arg(*mm.first  / 1000.0, 0, 'f', 3).arg(iMin + 1));
        cellDeltaLbl_->setText(QStringLiteral("%1 mV").arg(*mm.second - *mm.first, 0, 'f', 0));
        cellAvgLbl_->setText(QStringLiteral("%1 V").arg(avg / 1000.0, 0, 'f', 3));
    }

    QStringList rows;
    const int tn = std::min<int>(a.temp_count, static_cast<int>(a.temp_raw.size()));
    for (int k = 0; k < tn; ++k) {
        const double c = a.temp_raw[k] / 10.0 - 273.15;
        uint8_t ta = (k < static_cast<int>(alarm_.temp_alarm.size())) ? alarm_.temp_alarm[k] : 0;
        const QString tc = (ta == 0) ? QString() : (ta == 1 ? kAmber : kRed);
        QString val = QStringLiteral("%1 &deg;C").arg(c, 0, 'f', 1);
        if (!tc.isEmpty()) val = QStringLiteral("<b style='color:%1'>%2</b>").arg(tc, val);
        rows << QStringLiteral("<tr><td>%1&nbsp;&nbsp;&nbsp;</td><td align='right'>%2</td></tr>")
                    .arg(tr("Sensor %1").arg(k + 1), val);
    }
    tempsLbl_->setText(rows.isEmpty() ? QStringLiteral("–")
                                      : QStringLiteral("<table>") + rows.join(QString()) + QStringLiteral("</table>"));
}

// ── Alarms tab ──────────────────────────────────────────────────────────────

QWidget* MainWindow::buildAlarmsTab() {
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);

    auto* mosBox = new QGroupBox(tr("MOSFETs"));
    auto* mf = new QFormLayout(mosBox);
    chgMosLbl_ = new QLabel; dchgMosLbl_ = new QLabel; balLbl_ = new QLabel;
    mf->addRow(tr("Charge:"),    chgMosLbl_);
    mf->addRow(tr("Discharge:"), dchgMosLbl_);
    mf->addRow(tr("Balancing:"), balLbl_);
    grid->addWidget(mosBox, 0, 0);

    auto* hwBox = new QGroupBox(tr("Hardware faults"));
    auto* hf = new QFormLayout(hwBox);
    for (const auto& d : kHardware) {
        auto* l = new QLabel;
        hwLbls_.push_back(l);
        hf->addRow(tr(d.name) + QStringLiteral(":"), l);
    }
    grid->addWidget(hwBox, 1, 0);

    auto* protBox = new QGroupBox(tr("Protection events"));
    auto* pf = new QFormLayout(protBox);
    for (const auto& d : kProtections) {
        auto* l = new QLabel;
        protLbls_.push_back(l);
        pf->addRow(tr(d.name) + QStringLiteral(":"), l);
    }
    grid->addWidget(protBox, 0, 1, 2, 1);

    auto* cellBox = new QGroupBox(tr("Cell alarms"));
    auto* cl = new QVBoxLayout(cellBox);
    cellAlarmsLbl_ = new QLabel;
    cellAlarmsLbl_->setTextFormat(Qt::RichText);
    cellAlarmsLbl_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    cl->addWidget(cellAlarmsLbl_);
    grid->addWidget(cellBox, 0, 2);

    auto* stBox = new QGroupBox(tr("Pack limits & temperature alarms"));
    auto* sl = new QVBoxLayout(stBox);
    tempAlarmsLbl_ = new QLabel;
    tempAlarmsLbl_->setTextFormat(Qt::RichText);
    tempAlarmsLbl_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    sl->addWidget(tempAlarmsLbl_);
    grid->addWidget(stBox, 1, 2);

    grid->setRowStretch(2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    return page;
}

void MainWindow::updateAlarms() {
    const auto& s = alarm_.status;

    setOnOffLabel(chgMosLbl_,  (s[2] & 0x02) != 0);
    setOnOffLabel(dchgMosLbl_, (s[2] & 0x04) != 0);
    setOnOffLabel(balLbl_,     s[5] != 0 || s[6] != 0);

    for (size_t k = 0; k < protLbls_.size(); ++k) {
        const auto& d = kProtections[k];
        setOkLabel(protLbls_[k], !(s[d.byte] & d.mask));
    }
    for (size_t k = 0; k < hwLbls_.size(); ++k) {
        const auto& d = kHardware[k];
        setOkLabel(hwLbls_[k], !(s[d.byte] & d.mask));
    }

    // Cell grid: 8 cells per column
    const int n = alarm_.cell_count;
    if (n <= 0) {
        cellAlarmsLbl_->setText(QStringLiteral("–"));
    } else {
        const int perCol = 8;
        const int cols = (n + perCol - 1) / perCol;
        QString html = QStringLiteral("<table cellspacing='4'>");
        for (int r = 0; r < perCol && r < n; ++r) {
            html += QStringLiteral("<tr>");
            for (int c = 0; c < cols; ++c) {
                const int k = c * perCol + r;
                if (k >= n) { html += QStringLiteral("<td></td><td></td>"); continue; }
                const auto st = CellBarsWidget::cellState(alarm_, k);
                const QColor col = CellBarsWidget::stateColor(st, palette());
                html += QStringLiteral("<td>#%1</td><td style='color:%2'>&#9679;&nbsp;%3&nbsp;&nbsp;&nbsp;</td>")
                            .arg(k + 1).arg(col.name(), CellBarsWidget::stateText(st));
            }
            html += QStringLiteral("</tr>");
        }
        html += QStringLiteral("</table>");
        cellAlarmsLbl_->setText(html);
    }

    QString t = QStringLiteral("<table cellspacing='4'>");
    auto row = [&t](const QString& name, const QString& val) {
        t += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(name, val);
    };
    row(tr("Pack voltage"),      stateByteText(alarm_.total_volt_state));
    row(tr("Charge current"),    stateByteText(alarm_.charge_curr_state));
    row(tr("Discharge current"), stateByteText(alarm_.discharge_curr_state));
    const int tn = std::min<int>(alarm_.temp_count, static_cast<int>(alarm_.temp_alarm.size()));
    for (int k = 0; k < tn; ++k)
        row(tr("Temp sensor %1").arg(k + 1), stateByteText(alarm_.temp_alarm[k]));
    t += QStringLiteral("</table>");
    tempAlarmsLbl_->setText(t);
}

// ── Parameters tab ──────────────────────────────────────────────────────────

QWidget* MainWindow::buildParamsTab() {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    auto* bar = new QHBoxLayout;
    readBtn_   = new QPushButton(tr("Read from BMS"));
    writeBtn_  = new QPushButton(tr("Write changes…"));
    revertBtn_ = new QPushButton(tr("Discard changes"));
    connect(readBtn_, &QPushButton::clicked, this, [this] {
        if (dirty_.any()) {
            auto r = QMessageBox::question(this, tr("Unsaved changes"),
                tr("Reading from the BMS will discard your unsaved changes. Continue?"),
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (r != QMessageBox::Discard) return;
        }
        requestParams();
    });
    connect(writeBtn_,  &QPushButton::clicked, this, &MainWindow::writeParams);
    connect(revertBtn_, &QPushButton::clicked, this, &MainWindow::revertParams);
    bar->addWidget(readBtn_);
    bar->addWidget(writeBtn_);
    bar->addWidget(revertBtn_);
    bar->addSpacing(12);
    paramsInfo_ = new QLabel;
    paramsInfo_->setWordWrap(true);
    bar->addWidget(paramsInfo_, 1);
    v->addLayout(bar);

    auto* warn = new QLabel(tr(
        "Changed values are highlighted. Writes go to broadcast address 0, so <b>every pack on the bus</b> "
        "receives them. Only change values you understand – wrong protection settings can leave the "
        "battery unprotected or stop it charging/discharging."));
    warn->setWordWrap(true);
    warn->setTextFormat(Qt::RichText);
    QPalette wp = warn->palette();
    wp.setColor(QPalette::WindowText, wp.color(QPalette::PlaceholderText));
    warn->setPalette(wp);
    v->addWidget(warn);

    paramsBody_ = new QWidget;
    auto* cols = new QHBoxLayout(paramsBody_);
    auto* c1 = new QVBoxLayout;
    auto* c2 = new QVBoxLayout;
    cols->addLayout(c1, 1);
    cols->addLayout(c2, 1);
    buildParamGroups(c1, c2);
    c1->addStretch(1);
    c2->addStretch(1);

    auto* scroll = new QScrollArea;
    scroll->setWidget(paramsBody_);
    scroll->setWidgetResizable(true);
    v->addWidget(scroll, 1);

    paramsBody_->setEnabled(false);
    paramsInfo_->setText(tr("Connect, then open this tab or click <b>Read from BMS</b>."));
    return page;
}

QGroupBox* MainWindow::addGroup(QVBoxLayout* col, int gid, const QString& title, QFormLayout*& form) {
    auto* box = new QGroupBox(title);
    form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    col->addWidget(box);
    groupBoxes_[gid]  = box;
    groupTitles_[gid] = title;
    return box;
}

QCheckBox* MainWindow::addBool(QFormLayout* f, const QString& label, bool* v, int gid) {
    auto* cb = new QCheckBox(tr("Enabled"));
    f->addRow(label, cb);
    connect(cb, &QCheckBox::toggled, this, [this, v, gid](bool on) { *v = on; markDirty(gid); });
    editorLoaders_.push_back([cb, v] { QSignalBlocker b(cb); cb->setChecked(*v); });
    return cb;
}

QDoubleSpinBox* MainWindow::addDouble(QFormLayout* f, const QString& label, double* v, int gid,
                                      const QString& unit, int decimals, double min, double max, double step) {
    auto* sb = new SafeDoubleSpinBox;
    sb->setFocusPolicy(Qt::StrongFocus);
    sb->setDecimals(decimals);
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setMinimumWidth(130);
    sb->setAlignment(Qt::AlignRight);
    if (!unit.isEmpty()) sb->setSuffix(QStringLiteral(" ") + unit);
    f->addRow(label, sb);
    connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this, v, gid](double d) { *v = d; markDirty(gid); });
    editorLoaders_.push_back([sb, v] { QSignalBlocker b(sb); sb->setValue(*v); });
    return sb;
}

QSpinBox* MainWindow::addInt(QFormLayout* f, const QString& label, int* v, int gid,
                             const QString& unit, int min, int max, int step) {
    auto* sb = new SafeSpinBox;
    sb->setFocusPolicy(Qt::StrongFocus);
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setMinimumWidth(130);
    sb->setAlignment(Qt::AlignRight);
    if (!unit.isEmpty()) sb->setSuffix(QStringLiteral(" ") + unit);
    f->addRow(label, sb);
    connect(sb, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, v, gid](int i) { *v = i; markDirty(gid); });
    editorLoaders_.push_back([sb, v] { QSignalBlocker b(sb); sb->setValue(*v); });
    return sb;
}

void MainWindow::buildParamGroups(QVBoxLayout* c1, QVBoxLayout* c2) {
    using namespace pace;
    auto& p = params_;
    QFormLayout* f = nullptr;
    const QString V  = QStringLiteral("V");
    const QString A  = QStringLiteral("A");
    const QString ms = QStringLiteral("ms");
    const QString mV = QStringLiteral("mV");
    const QString C  = QStringLiteral("°C");

    // Value ranges follow the raw encodings in bms.cpp (uint16 / uint8 fields).
    auto voltGroup = [&](QVBoxLayout* col, int gid, const QString& title, CellVoltProtParams& g) {
        addGroup(col, gid, title, f);
        addBool  (f, tr("Protection:"), &g.enable, gid);
        addDouble(f, tr("Protect at:"), &g.protect_v, gid, V, 3, 0, 65.535, 0.01);
        addDouble(f, tr("Alarm at:"),   &g.alarm_v,   gid, V, 3, 0, 65.535, 0.01);
        addDouble(f, tr("Recover at:"), &g.recover_v, gid, V, 3, 0, 65.535, 0.01);
        addInt   (f, tr("Delay:"),      &g.delay_ms,  gid, ms, 0, 25500, 100);
    };
    auto currGroup = [&](QVBoxLayout* col, int gid, const QString& title, CurrProtParams& g) {
        addGroup(col, gid, title, f);
        addBool  (f, tr("Protection:"), &g.enable, gid);
        addDouble(f, tr("Protect at:"), &g.protect_a, gid, A, 2, 0, 655.35, 1);
        addDouble(f, tr("Alarm at:"),   &g.alarm_a,   gid, A, 2, 0, 655.35, 1);
        addDouble(f, tr("Recover at:"), &g.recover_a, gid, A, 2, 0, 655.35, 1);
        addInt   (f, tr("Delay:"),      &g.delay_ms,  gid, ms, 0, 25500, 100);
    };
    auto tempGroup = [&](QVBoxLayout* col, int gid, const QString& title, TempProtGroup& g, int sensors) {
        addGroup(col, gid, title, f);
        addBool(f, tr("Protection:"), &g.enable, gid);
        for (int k = 0; k < sensors; ++k)
            addInt(f, tr("Sensor %1:").arg(k + 1), &g.temp[k], gid, C, -273, 6280, 1);
    };

    // Column 1 — voltage & current
    voltGroup(c1, PG_CELL_OVP, tr("Cell over-voltage protection"), p.cell_ovp);
    voltGroup(c1, PG_PACK_OVP, tr("Pack over-voltage protection"), p.pack_ovp);
    currGroup(c1, PG_CHG_OCP,  tr("Charge over-current protection"), p.chg_ocp);
    currGroup(c1, PG_DCHG_OCP, tr("Discharge over-current protection"), p.dchg_ocp);

    addGroup(c1, PG_SCP, tr("Short-circuit protection"), f);
    addBool(f, tr("Protection:"), &p.scp.enable, PG_SCP);
    addInt (f, tr("Threshold:"),  &p.scp.threshold, PG_SCP, QString(), 0, 65535, 1);
    addInt (f, tr("Time:"),       &p.scp.time_us,   PG_SCP, QStringLiteral("µs"), 0, 65535, 1);
    addInt (f, tr("Delay:"),      &p.scp.delay_ms,  PG_SCP, ms, 0, 25500, 100);

    addGroup(c1, PG_BALANCE, tr("Balancing"), f);
    addDouble(f, tr("Start above:"), &p.balance.threshold_v, PG_BALANCE, V, 3, 0, 65.535, 0.01);
    addInt   (f, tr("Cell delta:"),  &p.balance.delta_mv,    PG_BALANCE, mV, 0, 65535, 1);

    addGroup(c1, PG_PACK_B, tr("Pack params B"), f);
    addDouble(f, tr("Voltage:"), &p.pack_b.volt_v, PG_PACK_B, V, 3, 0, 65.535, 0.01);
    addInt   (f, tr("Value:"),   &p.pack_b.value,  PG_PACK_B, QString(), 0, 65535, 1);

    addGroup(c1, PG_EQUALIZATION, tr("Pack equalization"), f);
    addDouble(f, tr("Start voltage:"), &p.equalization.start_v,       PG_EQUALIZATION, V, 3, 0, 65.535, 0.01);
    addInt   (f, tr("Pack delta:"),    &p.equalization.delta_pack_mv, PG_EQUALIZATION, mV, 0, 65535, 1);
    addInt   (f, tr("Cell count:"),    &p.equalization.cell_cnt,      PG_EQUALIZATION, QString(), 0, 255, 1);

    // Column 2 — temperatures
    addGroup(c2, PG_CHG_OTP, tr("Charge over-temperature protection"), f);
    addBool(f, tr("Protection:"), &p.chg_otp.enable, PG_CHG_OTP);
    addInt (f, tr("Trigger at:"), &p.chg_otp.trigger_c, PG_CHG_OTP, C, 0, 32767, 1);
    addInt (f, tr("Recover at:"), &p.chg_otp.recover_c, PG_CHG_OTP, C, 0, 32767, 1);
    addInt (f, tr("Delay:"),      &p.chg_otp.delay_ms,  PG_CHG_OTP, ms, 0, 25500, 100);

    addGroup(c2, PG_MOSFET_OTP, tr("MOSFET over-temperature protection"), f);
    addInt(f, tr("Trigger at:"), &p.mosfet_otp.trigger_c, PG_MOSFET_OTP, C, 0, 32767, 1);
    addInt(f, tr("Delay:"),      &p.mosfet_otp.delay_ms,  PG_MOSFET_OTP, ms, 0, 6375, 25);

    addGroup(c2, PG_UTP, tr("Under-temperature protection"), f);
    addInt(f, tr("Delay:"), &p.utp.delay_ms, PG_UTP, ms, 0, 6375, 25);

    tempGroup(c2, PG_CHG_TEMP,      tr("Charge temperature protection"),    p.chg_temp, 6);
    tempGroup(c2, PG_DCHG_TEMP,     tr("Discharge temperature protection"), p.dchg_temp, 6);
    tempGroup(c2, PG_MOS_CHG_TEMP,  tr("MOS charge temperature"),           p.mos_chg_temp, 3);
    tempGroup(c2, PG_MOS_DCHG_TEMP, tr("MOS discharge temperature"),        p.mos_dchg_temp, 6);
}

void MainWindow::loadEditorsFromParams() {
    for (auto& load : editorLoaders_) load();
    groupBoxes_[pace::PG_MOS_CHG_TEMP]->setVisible(params_.mos_chg_temp.present);
    groupBoxes_[pace::PG_MOS_DCHG_TEMP]->setVisible(params_.mos_dchg_temp.present);
}

void MainWindow::markDirty(int gid) {
    dirty_.set(gid);
    refreshDirtyUi();
}

void MainWindow::refreshDirtyUi() {
    for (int gid = 0; gid < pace::PG_COUNT; ++gid) {
        QGroupBox* box = groupBoxes_[gid];
        if (!box) continue;
        const bool d = dirty_.get(gid);
        box->setTitle(d ? groupTitles_[gid] + tr("  • modified") : groupTitles_[gid]);
        box->setStyleSheet(d ? QStringLiteral("QGroupBox { border:1px solid %1; border-radius:4px; margin-top:1.2em; }"
                                              "QGroupBox::title { color:%1; subcontrol-origin: margin; left: 8px; }").arg(kAmber)
                             : QString());
    }
    updateParamsButtons();
}

void MainWindow::updateParamsButtons() {
    int n = 0;
    for (int gid = 0; gid < pace::PG_COUNT; ++gid) n += dirty_.get(gid) ? 1 : 0;
    readBtn_->setEnabled(connected_ && !writing_);
    writeBtn_->setEnabled(connected_ && paramsValid_ && n > 0 && !writing_);
    writeBtn_->setText(n > 0 ? tr("Write %n changed group(s)…", nullptr, n) : tr("Write changes…"));
    revertBtn_->setEnabled(n > 0 && !writing_);
    paramsBody_->setEnabled(paramsValid_ && !writing_);
    packCombo_->setEnabled(connected_ && packCount_ > 1 && !writing_);
}

void MainWindow::requestParams() {
    if (!connected_) return;
    const int pack = currentPack();
    paramsInfo_->setText(tr("Reading parameters from pack %1…").arg(pack));
    paramsInfo_->setStyleSheet(QString());
    readBtn_->setEnabled(false);
    dispatch([w = worker_, pack] { w->loadParams(pack); });
}

void MainWindow::writeParams() {
    if (!dirty_.any() || !connected_) return;

    QStringList changed;
    for (int gid = 0; gid < pace::PG_COUNT; ++gid)
        if (dirty_.get(gid)) changed << QStringLiteral("  • ") + groupTitles_[gid];

    auto r = QMessageBox::warning(this, tr("Write to BMS"),
        tr("Write these parameter groups to the BMS?\n\n%1\n\n"
           "They are sent to broadcast address 0, so every pack on the bus receives them. "
           "The values will be read back afterwards so you can check them.")
            .arg(changed.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (r != QMessageBox::Yes) return;

    writing_ = true;
    updateParamsButtons();
    paramsInfo_->setText(tr("Writing…"));
    paramsInfo_->setStyleSheet(QString());
    const int pack = currentPack();
    const pace::AllParams p = params_;
    const pace::DirtyFlags d = dirty_;
    dispatch([w = worker_, pack, p, d] { w->saveParams(pack, p, d); });
}

void MainWindow::revertParams() {
    params_ = paramsOrig_;
    dirty_.clear();
    loadEditorsFromParams();
    refreshDirtyUi();
}

// ── Pack / tab switching ────────────────────────────────────────────────────

int MainWindow::currentPack() const {
    return packCombo_->currentIndex() + 1;
}

void MainWindow::onPackChanged(int index) {
    if (index < 0 || index == lastPackIndex_) return;

    if (dirty_.any()) {
        auto r = QMessageBox::question(this, tr("Unsaved changes"),
            tr("Switch pack and discard parameter changes that were not written?"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (r != QMessageBox::Discard) {
            QSignalBlocker b(packCombo_);
            packCombo_->setCurrentIndex(lastPackIndex_);
            return;
        }
        revertParams();
    }
    lastPackIndex_ = index;

    clearLive();
    paramsValid_ = false;
    paramsPack_  = -1;
    updateParamsButtons();
    requestPoll(true);
    if (tabs_->currentIndex() == 2) requestParams();
}

void MainWindow::onTabChanged(int index) {
    if (index == 2 && connected_ && (!paramsValid_ || paramsPack_ != currentPack()) && !dirty_.any())
        requestParams();
}

// ── Polling ─────────────────────────────────────────────────────────────────

void MainWindow::requestPoll(bool force) {
    if (!connected_) return;
    if (pending_ > 0 && !force) return;   // previous request still running
    const int pack = currentPack();
    dispatch([w = worker_, pack] { w->poll(pack); });
}

// ── Worker results ──────────────────────────────────────────────────────────

void MainWindow::onOpened(const QString& port, int packCount, const QString& version) {
    connecting_ = false;
    connected_  = true;
    port_       = port;
    packCount_  = std::clamp(packCount, 1, 15);

    {
        QSignalBlocker b(packCombo_);
        packCombo_->clear();
        for (int i = 1; i <= packCount_; ++i) packCombo_->addItem(QString::number(i));
        packCombo_->setCurrentIndex(0);
    }
    lastPackIndex_ = 0;
    paramsValid_   = false;
    paramsPack_    = -1;

    QSettings().setValue(QStringLiteral("port"), port);

    setConnectedUi(true);
    QString msg = tr("Connected to %1 – %n pack(s) found", nullptr, packCount_).arg(port);
    if (!version.isEmpty()) msg += tr(" – firmware %1").arg(version);
    showStatus(msg);

    requestPoll(true);
    if (tabs_->currentIndex() == 2) requestParams();
}

void MainWindow::onOpenFailed(const QString& error) {
    connecting_ = false;
    connected_  = false;
    setConnectedUi(false);
    showStatus(tr("Connection failed"), true);
    QMessageBox::warning(this, tr("Connection failed"), error);
}

void MainWindow::onClosed() {
    if (connecting_) return;   // re-open in progress
    connected_ = false;
    paramsValid_ = false;
    paramsPack_  = -1;
    dirty_.clear();
    refreshDirtyUi();
    setConnectedUi(false);
    clearLive();
    showStatus(tr("Disconnected"));
}

void MainWindow::onLiveData(int pack, const pace::AnalogData& analog, const pace::AlarmData& alarm) {
    if (!connected_ || pack != currentPack()) return;
    analog_    = analog;
    alarm_     = alarm;
    liveValid_ = true;
    lastUpdate_ = QDateTime::currentDateTime();
    liveBanner_->hide();
    updateLive();
    updateAlarms();
    updateLastUpdateLabel();
}

void MainWindow::onLiveFailed(int pack, const QString& error) {
    if (!connected_ || pack != currentPack()) return;
    liveBanner_->setText(tr("No answer from pack %1: %2%3")
        .arg(pack).arg(error,
             liveValid_ ? tr(" – showing the last good reading.") : QString()));
    liveBanner_->show();
}

void MainWindow::onParamsLoaded(int pack, const pace::AllParams& params) {
    if (pack != currentPack()) { writing_ = false; updateParamsButtons(); return; }
    params_      = params;
    paramsOrig_  = params;
    dirty_.clear();
    paramsValid_ = true;
    paramsPack_  = pack;
    loadEditorsFromParams();
    refreshDirtyUi();
    if (!writing_) {
        paramsInfo_->setText(tr("Read from pack %1 at %2.")
            .arg(pack).arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
        paramsInfo_->setStyleSheet(QString());
    }
    writing_ = false;
    updateParamsButtons();
}

void MainWindow::onParamsFailed(int pack, const QString& error) {
    if (pack != currentPack()) { writing_ = false; updateParamsButtons(); return; }
    writing_ = false;
    paramsInfo_->setText(tr("Could not read parameters: %1").arg(error));
    paramsInfo_->setStyleSheet(QStringLiteral("color:%1").arg(kRed));
    updateParamsButtons();
}

void MainWindow::onParamsSaved(int pack) {
    paramsInfo_->setText(tr("Written to the BMS at %1, values below were read back from pack %2.")
        .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))).arg(pack));
    paramsInfo_->setStyleSheet(QStringLiteral("color:%1").arg(kGreen));
    showStatus(tr("Parameters written"));
}

void MainWindow::onSaveFailed(int /*pack*/, const QString& error) {
    writing_ = false;
    updateParamsButtons();
    paramsInfo_->setText(tr("Write failed: %1").arg(error));
    paramsInfo_->setStyleSheet(QStringLiteral("color:%1").arg(kRed));
    QMessageBox::critical(this, tr("Write failed"),
        tr("Writing to the BMS failed:\n\n%1\n\nSome groups may already have been written. "
           "Your edits are kept – click \"Read from BMS\" to see what the BMS holds now.").arg(error));
}

void MainWindow::onRequestDone() {
    pending_ = std::max(0, pending_ - 1);
}

// ── Misc ────────────────────────────────────────────────────────────────────

void MainWindow::setConnectedUi(bool c) {
    connectBtn_->setText(c ? tr("Disconnect") : tr("Connect"));
    connectBtn_->setEnabled(true);
    portCombo_->setEnabled(!c);
    baudCombo_->setEnabled(!c);
    rescanBtn_->setEnabled(!c);
    packCombo_->setEnabled(c && packCount_ > 1 && !writing_);
    refreshBtn_->setEnabled(c);
    if (c && autoRefresh_->isChecked()) pollTimer_.start();
    else pollTimer_.stop();
    updateParamsButtons();
}

void MainWindow::showStatus(const QString& msg, bool error) {
    statusLbl_->setText(msg);
    statusLbl_->setStyleSheet(error ? QStringLiteral("color:%1").arg(kRed) : QString());
}

void MainWindow::updateLastUpdateLabel() {
    if (!lastUpdate_.isValid()) { lastUpdLbl_->clear(); return; }
    const qint64 secs = lastUpdate_.secsTo(QDateTime::currentDateTime());
    lastUpdLbl_->setText(tr("Last update %1 (%2 s ago)")
        .arg(lastUpdate_.toString(QStringLiteral("HH:mm:ss"))).arg(secs));
}
