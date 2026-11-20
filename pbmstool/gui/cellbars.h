#pragma once
#include "bms.h"
#include <QWidget>

// Draws one horizontal bar per cell, coloured by alarm / balancing state.
class CellBarsWidget : public QWidget {
    Q_OBJECT
public:
    explicit CellBarsWidget(QWidget* parent = nullptr);

    void setData(const pace::AnalogData& analog, const pace::AlarmData& alarm);
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Cell state shared with the Alarms tab.
    enum class CellState { Normal, Balancing, OvpWarn, OvpFault, UvpWarn, UvpFault };
    static CellState cellState(const pace::AlarmData& alarm, int cell);
    static QString   stateText(CellState s);
    static QColor    stateColor(CellState s, const QPalette& pal);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kRowHeight = 24;

    pace::AnalogData analog_;
    pace::AlarmData  alarm_;
    bool hasData_ = false;
};
