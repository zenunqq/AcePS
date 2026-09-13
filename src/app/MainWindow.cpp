/*
 * MainWindow.cpp implements the initial AcePS desktop shell. It deliberately
 * keeps game discovery and emulation actions as clearly marked future seams.
 */
#include "aceps/app/MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>

namespace aceps::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), library_(std::filesystem::current_path() / ".aceps" / "games") {
  setWindowTitle(QStringLiteral("AcePS"));
  resize(1100, 700);
  createMenus();
  createToolbar();
  createLibraryView();
  statusLabel_ = new QLabel(tr("Ready — no game selected"), this);
  statusBar()->addPermanentWidget(statusLabel_);
}

void MainWindow::createMenus() {
  auto* fileMenu = menuBar()->addMenu(tr("&File"));
  auto* addAction = fileMenu->addAction(tr("&Add Game..."));
  connect(addAction, &QAction::triggered, this, &MainWindow::showAddGameDialog);
  fileMenu->addSeparator();
  auto* quitAction = fileMenu->addAction(tr("&Quit"));
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
  auto* settingsAction = toolsMenu->addAction(tr("&Settings"));
  connect(settingsAction, &QAction::triggered, this, [this] {
    QMessageBox::information(this, tr("Settings"), tr("Settings UI is planned for the next milestone."));
  });

  auto* helpMenu = menuBar()->addMenu(tr("&Help"));
  auto* aboutAction = helpMenu->addAction(tr("&About AcePS"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::createToolbar() {
  auto* toolbar = addToolBar(tr("Main Toolbar"));
  toolbar->setMovable(false);
  auto* addAction = toolbar->addAction(tr("Add Game"));
  connect(addAction, &QAction::triggered, this, &MainWindow::showAddGameDialog);
  auto* playAction = toolbar->addAction(tr("Play"));
  connect(playAction, &QAction::triggered, this, [this] {
    QMessageBox::information(this, tr("Play"), tr("Select a game after game loading is implemented."));
  });
  toolbar->addSeparator();
  auto* refreshAction = toolbar->addAction(tr("Refresh Library"));
  connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshLibrary);
}

void MainWindow::createLibraryView() {
  auto* container = new QWidget(this);
  auto* layout = new QVBoxLayout(container);
  auto* heading = new QLabel(tr("Game Library"), container);
  heading->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 600;"));
  layout->addWidget(heading);

  gameList_ = new QListWidget(container);
  gameList_->setViewMode(QListView::IconMode);
  gameList_->setResizeMode(QListView::Adjust);
  gameList_->setSpacing(12);
  gameList_->setMinimumHeight(400);
  auto* emptyState = new QListWidgetItem(tr("Click 'Add Game' to get started"), gameList_);
  emptyState->setFlags(Qt::NoItemFlags);
  layout->addWidget(gameList_);
  setCentralWidget(container);
  refreshLibrary();
}

void MainWindow::refreshLibrary() {
  if (gameList_ == nullptr) return;
  std::string error;
  if (!library_.refresh(error)) {
    if (statusLabel_ != nullptr) {
      statusLabel_->setText(tr("Library error: %1").arg(QString::fromStdString(error)));
    }
    return;
  }
  gameList_->clear();
  if (library_.entries().empty()) {
    auto* emptyState = new QListWidgetItem(tr("Click 'Add Game' to get started"), gameList_);
    emptyState->setFlags(Qt::NoItemFlags);
    if (statusLabel_ != nullptr) statusLabel_->setText(tr("No games found"));
    return;
  }
  for (const auto& game : library_.entries()) {
    gameList_->addItem(QString::fromStdString(game.displayName));
  }
  if (statusLabel_ != nullptr) {
    statusLabel_->setText(tr("%1 game(s) found").arg(static_cast<int>(library_.entries().size())));
  }
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(this, tr("About AcePS"),
                     tr("AcePS %1\nAn original PS4 emulator research project.")
                         .arg(QStringLiteral(ACEPS_VERSION)));
}

void MainWindow::showAddGameDialog() {
  const auto path = QFileDialog::getExistingDirectory(this, tr("Select PS4 Game Folder"));
  if (!path.isEmpty()) {
    statusLabel_->setText(tr("Game folder selected: %1").arg(path));
  }
}

} // namespace aceps::app
