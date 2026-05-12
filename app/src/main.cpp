#include <QApplication>

#include "cdmanager/presentation/mainwindow/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MultiLanguageCDManager"));
    QApplication::setOrganizationName(QStringLiteral("Epicreds"));

    cdmanager::presentation::mainwindow::MainWindow window;
    window.show();

    return app.exec();
}
