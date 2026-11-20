#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PbmsTools"));
    QApplication::setApplicationName(QStringLiteral("pbmstool-gui"));
    QApplication::setApplicationDisplayName(QStringLiteral("PbmsTools"));
    QApplication::setDesktopFileName(QStringLiteral("pbmstool-gui"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("battery")));

    MainWindow w;
    w.show();
    return app.exec();
}
