/*
 * MainWindow.h declares the AcePS Games home screen: console-style navigation,
 * hero selection, game cards, search, settings, and a top-right import action.
 */
#pragma once

#include "aceps/app/GameLibrary.h"

#include <QMainWindow>

#include <filesystem>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace aceps::app {

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  void createHeader();
  void createLibraryView();
  void refreshLibrary();
  void importGame();
  void selectGame(int row);
  void showSettings();
  void showAboutDialog();
  void bootSelectedGame();
  void updateHero(const GameEntry* game);
  void addImportedGame(const std::filesystem::path& path);

  GameLibrary library_;
  QListWidget* gameList_{nullptr};
  QLineEdit* searchBox_{nullptr};
  QLabel* heroArtwork_{nullptr};
  QLabel* heroTitle_{nullptr};
  QLabel* heroSubtitle_{nullptr};
  QLabel* emptyState_{nullptr};
  QLabel* statusLabel_{nullptr};
  QPushButton* bootButton_{nullptr};
  QToolButton* importButton_{nullptr};
};

} // namespace aceps::app
