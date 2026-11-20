#include "cellbars.h"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>

CellBarsWidget::CellBarsWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void CellBarsWidget::setData(const pace::AnalogData& analog, const pace::AlarmData& alarm) {
    analog_  = analog;
    alarm_   = alarm;
    hasData_ = true;
    updateGeometry();
    update();
}

void CellBarsWidget::clear() {
    hasData_ = false;
    analog_  = pace::AnalogData{};
    alarm_   = pace::AlarmData{};
    updateGeometry();
    update();
}

QSize CellBarsWidget::sizeHint() const {
    int rows = hasData_ ? std::max(analog_.cell_count, 1) : 16;
    return QSize(420, rows * kRowHeight + 8);
}

QSize CellBarsWidget::minimumSizeHint() const {
    int rows = hasData_ ? std::max(analog_.cell_count, 1) : 1;
    return QSize(300, rows * kRowHeight + 8);
}

CellBarsWidget::CellState CellBarsWidget::cellState(const pace::AlarmData& alarm, int k) {
    uint8_t va = (k < static_cast<int>(alarm.cell_volt_alarm.size())) ? alarm.cell_volt_alarm[k] : 0;
    // Balancing flags: status byte 5 = cells 1-8, byte 6 = cells 9-16
    bool balancing = false;
    if (k < 16) {
        int byteIdx = (k < 8) ? 5 : 6;
        balancing = (alarm.status[byteIdx] >> (k % 8)) & 1;
    }
    if (va & 0x02) return CellState::UvpFault;
    if (va & 0x08) return CellState::OvpFault;
    if (va & 0x01) return CellState::UvpWarn;
    if (va & 0x04) return CellState::OvpWarn;
    if (balancing) return CellState::Balancing;
    return CellState::Normal;
}

QString CellBarsWidget::stateText(CellState s) {
    switch (s) {
        case CellState::UvpFault:  return QStringLiteral("UVP!");
        case CellState::OvpFault:  return QStringLiteral("OVP!");
        case CellState::UvpWarn:   return QStringLiteral("Low warn");
        case CellState::OvpWarn:   return QStringLiteral("High warn");
        case CellState::Balancing: return QStringLiteral("Balancing");
        case CellState::Normal:    break;
    }
    return QStringLiteral("OK");
}

QColor CellBarsWidget::stateColor(CellState s, const QPalette& pal) {
    switch (s) {
        case CellState::UvpFault:
        case CellState::OvpFault:  return QColor(0xd3, 0x2f, 0x2f);   // red
        case CellState::UvpWarn:
        case CellState::OvpWarn:   return QColor(0xf5, 0x9e, 0x0b);   // amber
        case CellState::Balancing: return QColor(0x00, 0xac, 0xc1);   // cyan
        case CellState::Normal:    break;
    }
    Q_UNUSED(pal);
    return QColor(0x43, 0xa0, 0x47);                                   // green
}

void CellBarsWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = palette();

    if (!hasData_ || analog_.cell_count <= 0) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("No cell data yet"));
        return;
    }

    const auto& mv = analog_.cell_mv;
    const int n = std::min<int>(analog_.cell_count, static_cast<int>(mv.size()));
    if (n <= 0) return;

    double vMin = *std::min_element(mv.begin(), mv.begin() + n);
    double vMax = *std::max_element(mv.begin(), mv.begin() + n);
    int idxMin = static_cast<int>(std::min_element(mv.begin(), mv.begin() + n) - mv.begin());
    int idxMax = static_cast<int>(std::max_element(mv.begin(), mv.begin() + n) - mv.begin());

    // Same scaling as the TUI: zoom in around the actual spread so small
    // imbalances are visible.
    double lo = vMin - 50.0, hi = vMax + 50.0;
    if (hi - lo < 100.0) { lo -= 50.0; hi += 50.0; }

    QFontMetrics fm(font());
    const int labelW = fm.horizontalAdvance(QStringLiteral("#16")) + 12;
    const int valueW = fm.horizontalAdvance(QStringLiteral("0.000 V")) + 16;
    const int tagW   = fm.horizontalAdvance(QStringLiteral("Balancing  max")) + 12;
    const int barX   = labelW + valueW;
    const int barW   = std::max(40, width() - barX - tagW - 8);

    for (int k = 0; k < n; ++k) {
        const int y = 4 + k * kRowHeight;
        const CellState st = cellState(alarm_, k);
        const QColor col = stateColor(st, pal);

        p.setPen(pal.color(QPalette::Text));
        p.drawText(QRect(4, y, labelW - 4, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("#%1").arg(k + 1));
        p.drawText(QRect(labelW, y, valueW - 8, kRowHeight), Qt::AlignVCenter | Qt::AlignRight,
                   QString::number(mv[k] / 1000.0, 'f', 3) + QStringLiteral(" V"));

        // Track
        QRectF track(barX, y + 5, barW, kRowHeight - 10);
        QColor trackCol = pal.color(QPalette::Mid);
        trackCol.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(trackCol);
        p.drawRoundedRect(track, 4, 4);

        // Fill
        double frac = (hi > lo) ? (mv[k] - lo) / (hi - lo) : 0.0;
        frac = std::clamp(frac, 0.0, 1.0);
        QRectF fill(track.x(), track.y(), std::max(6.0, track.width() * frac), track.height());
        p.setBrush(col);
        p.drawRoundedRect(fill, 4, 4);

        // Tag
        QString tag = (st == CellState::Normal) ? QString() : stateText(st);
        if (n > 1 && vMax - vMin > 0.5) {
            if (k == idxMax) tag += tag.isEmpty() ? QStringLiteral("max") : QStringLiteral("  max");
            if (k == idxMin) tag += tag.isEmpty() ? QStringLiteral("min") : QStringLiteral("  min");
        }
        if (!tag.isEmpty()) {
            p.setPen(st == CellState::Normal ? pal.color(QPalette::PlaceholderText) : col);
            p.drawText(QRect(barX + barW + 8, y, tagW, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, tag);
        }
    }
}
