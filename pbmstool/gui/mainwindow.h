#pragma once
#include "params.h"
#include "worker.h"

#include <QDateTime>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QVBoxLayout;
class QWidget;
class CellBarsWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // ── UI construction ──
    QWidget* buildToolbar();
    QWidget* buildLiveTab();
    QWidget* buildAlarmsTab();
    QWidget* buildParamsTab();
    void     buildParamGroups(QVBoxLayout* col1, QVBoxLayout* col2);

    QGroupBox*      addGroup(QVBoxLayout* col, int gid, const QString& title, QFormLayout*& form);
    QCheckBox*      addBool  (QFormLayout* f, const QString& label, bool* v, int gid);
    QDoubleSpinBox* addDouble(QFormLayout* f, const QString& label, double* v, int gid,
                              const QString& unit, int decimals, double min, double max, double step);
    QSpinBox*       addInt   (QFormLayout* f, const QString& label, int* v, int gid,
                              const QString& unit, int min, int max, int step);

    // ── Actions ──
    void rescanPorts();
    void toggleConnection();
    void requestPoll(bool force = false);
    void requestParams();
    void writeParams();
    void revertParams();
    void onPackChanged(int index);
    void onTabChanged(int index);

    // ── Worker results ──
    void onOpened(const QString& port, int packCount, const QString& version);
    void onOpenFailed(const QString& error);
    void onClosed();
    void onLiveData(int pack, const pace::AnalogData& analog, const pace::AlarmData& alarm);
    void onLiveFailed(int pack, const QString& error);
    void onParamsLoaded(int pack, const pace::AllParams& params);
    void onParamsFailed(int pack, const QString& error);
    void onParamsSaved(int pack);
    void onSaveFailed(int pack, const QString& error);
    void onRequestDone();

    // ── Helpers ──
    int  currentPack() const;
    void setConnectedUi(bool connected);
    void updateLive();
    void updateAlarms();
    void clearLive();
    void markDirty(int gid);
    void refreshDirtyUi();
    void loadEditorsFromParams();
    void updateParamsButtons();
    void showStatus(const QString& msg, bool error = false);
    void updateLastUpdateLabel();
    template <typename F> void dispatch(F&& fn);

    // ── Worker / thread ──
    QThread thread_;
    Worker* worker_ = nullptr;
    int     pending_ = 0;

    // ── Connection state ──
    bool    connected_    = false;
    bool    connecting_   = false;
    QString port_;
    int     packCount_    = 1;
    int     lastPackIndex_ = 0;

    // ── Toolbar ──
    QComboBox*   portCombo_    = nullptr;
    QComboBox*   baudCombo_    = nullptr;
    QPushButton* rescanBtn_    = nullptr;
    QPushButton* connectBtn_   = nullptr;
    QComboBox*   packCombo_    = nullptr;
    QCheckBox*   autoRefresh_  = nullptr;
    QSpinBox*    intervalSpin_ = nullptr;
    QPushButton* refreshBtn_   = nullptr;
    QTabWidget*  tabs_         = nullptr;
    QLabel*      statusLbl_    = nullptr;
    QLabel*      lastUpdLbl_   = nullptr;

    QTimer       pollTimer_;
    QTimer       ageTimer_;
    QDateTime    lastUpdate_;

    // ── Live tab ──
    CellBarsWidget* cells_      = nullptr;
    QLabel*       packVoltLbl_  = nullptr;
    QLabel*       currentLbl_   = nullptr;
    QLabel*       powerLbl_     = nullptr;
    QProgressBar* socBar_       = nullptr;
    QLabel*       remainLbl_    = nullptr;
    QLabel*       fullLbl_      = nullptr;
    QLabel*       designLbl_    = nullptr;
    QLabel*       cyclesLbl_    = nullptr;
    QLabel*       cellMinLbl_   = nullptr;
    QLabel*       cellMaxLbl_   = nullptr;
    QLabel*       cellDeltaLbl_ = nullptr;
    QLabel*       cellAvgLbl_   = nullptr;
    QLabel*       tempsLbl_     = nullptr;
    QLabel*       liveBanner_   = nullptr;

    // ── Alarms tab ──
    QLabel* chgMosLbl_  = nullptr;
    QLabel* dchgMosLbl_ = nullptr;
    QLabel* balLbl_     = nullptr;
    std::vector<QLabel*> protLbls_;
    std::vector<QLabel*> hwLbls_;
    QLabel* cellAlarmsLbl_ = nullptr;
    QLabel* tempAlarmsLbl_ = nullptr;

    // ── Params tab ──
    QPushButton* readBtn_     = nullptr;
    QPushButton* writeBtn_    = nullptr;
    QPushButton* revertBtn_   = nullptr;
    QLabel*      paramsInfo_  = nullptr;
    QWidget*     paramsBody_  = nullptr;
    std::array<QGroupBox*, pace::PG_COUNT> groupBoxes_{};
    std::array<QString,    pace::PG_COUNT> groupTitles_{};
    std::vector<std::function<void()>> editorLoaders_;

    pace::AllParams  params_;
    pace::AllParams  paramsOrig_;
    pace::DirtyFlags dirty_;
    bool paramsValid_ = false;
    bool writing_     = false;
    int  paramsPack_  = -1;

    // ── Latest live data ──
    pace::AnalogData analog_;
    pace::AlarmData  alarm_;
    bool liveValid_ = false;
};
