// AcePS desktop entry point: starts Qt and presents the emulator's initial user interface.
#include "interface/shell/main_window.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argumentCount, char* argumentValues[]) {
    QCoreApplication::setOrganizationName("AcePS Project");
    QCoreApplication::setApplicationName("AcePS");

    QApplication application(argumentCount, argumentValues);
    AcePS::Interface::MainWindow mainWindow;
    mainWindow.show();
    return application.exec();
}
