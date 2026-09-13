/*
 * MainWindow.h declares AcePS's first-run desktop shell: library view,
 * menu bar, toolbar, and status bar. Emulation services remain UI-agnostic.
 */
#pragma once

#include "aceps/app/GameLibrary.h"

#include <QMainWindow>

class QListWidget;
class QLabel;

namespace aceps::app {

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  void createMenus();
  void createToolbar();
  void createLibraryView();
  void refreshLibrary();
  void showAboutDialog();
  void showAddGameDialog();

  GameLibrary library_;
  QListWidget* gameList_{nullptr};
  QLabel* statusLabel_{nullptr};
};

} // namespace aceps::app
