/*
 * main.cpp is the AcePS process entry point. It initializes logging, creates
 * the Qt application and emulator lifecycle, and shows the main window.
 */
#include <QApplication>
#include <QMessageBox>

#include "aceps/app/MainWindow.h"
#include "aceps/common/Logging.h"
#include "aceps/core/Emulator.h"
#include "aceps/core/EmulatorError.h"

int main(int argc, char* argv[]) {
  aceps::logging::initialize();
  QApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("AcePS"));
  application.setApplicationVersion(QStringLiteral(ACEPS_VERSION));

  aceps::core::Emulator emulator;
  try {
    emulator.initialize();
  } catch (const aceps::core::EmulatorError& error) {
    aceps::logging::error(error.what());
    QMessageBox::critical(nullptr, QObject::tr("AcePS Startup Error"),
                          QObject::tr("AcePS could not initialize:\n%1").arg(QString::fromUtf8(error.what())));
    aceps::logging::shutdown();
    return 1;
  }

  aceps::app::MainWindow window;
  window.show();
  const int result = application.exec();
  emulator.shutdown();
  aceps::logging::shutdown();
  return result;
}
