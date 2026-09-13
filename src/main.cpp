/*
 * main.cpp is the AcePS process entry point. It initializes logging, creates
 * the Qt application and emulator lifecycle, and shows the main window.
 */
#include <QApplication>
#include <QMessageBox>

#include "aceps/app/MainWindow.h"
#include "aceps/common/Logging.h"
#include "aceps/core/BootSequence.h"
#include "aceps/core/Emulator.h"
#include "aceps/core/EmulatorError.h"

#include <iostream>

int main(int argc, char* argv[]) {
  aceps::logging::initialize();

  if (argc > 1) {
    auto* previousClogBuffer = std::clog.rdbuf(std::cout.rdbuf());
    aceps::logging::info(std::string("CLI boot requested for ") + argv[1]);
    aceps::core::BootSequence bootSequence;
    std::string error;
    const bool succeeded = bootSequence.run(argv[1], error);
    if (succeeded) {
      aceps::logging::info("CLI boot completed successfully");
    } else {
      aceps::logging::error(std::string("CLI boot failed: ") + error);
    }
    std::clog.rdbuf(previousClogBuffer);
    aceps::logging::shutdown();
    return succeeded ? 0 : 1;
  }

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
  window.showMaximized();
  const int result = application.exec();
  emulator.shutdown();
  aceps::logging::shutdown();
  return result;
}
