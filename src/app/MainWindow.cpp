/*
 * MainWindow.cpp implements the AcePS Games home screen. It intentionally
 * removes store/media distractions and makes importing a local game folder the
 * primary action. Game artwork is read from sce_sys/icon0.png when available.
 */
#include "aceps/app/MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace aceps::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), library_(std::filesystem::current_path() / ".aceps" / "games") {
  setWindowTitle(QStringLiteral("AcePS"));
  setMinimumSize(1080, 680);
  resize(1440, 820);
  setStyleSheet(QStringLiteral(R"(
    QMainWindow, QWidget { background: #0b1220; color: #f3f7ff; }
    QMenuBar { background: #101a2b; color: #aebbd0; padding: 7px 16px; border-bottom: 1px solid #233149; }
    QMenuBar::item:selected { background: #1c2a43; color: #ffffff; border-radius: 6px; }
    QToolButton { border: 0; border-radius: 18px; padding: 9px 15px; color: #c7d5e8; }
    QToolButton:hover { background: #1b2e4a; color: #ffffff; }
    QLineEdit { background: #142238; border: 1px solid #2b4160; border-radius: 18px; padding: 9px 16px; color: #ffffff; selection-background-color: #27a6ff; }
    QListWidget { background: transparent; border: 0; outline: 0; }
    QListWidget::item { background: #111e33; border: 1px solid #1e3552; border-radius: 12px; margin: 8px; padding: 0; }
    QListWidget::item:selected { border: 2px solid #31b7ff; background: #162b47; }
    QStatusBar { background: #0a101b; color: #8496ad; border-top: 1px solid #1d2a3e; }
  )"));
  createHeader();
  createLibraryView();
  statusLabel_ = new QLabel(tr("Ready — add a game folder to begin"), this);
  statusBar()->addWidget(statusLabel_);
  refreshLibrary();
}

void MainWindow::createHeader() {
  auto* fileMenu = menuBar()->addMenu(tr("File"));
  auto* importAction = fileMenu->addAction(tr("Import Game Folder..."));
  connect(importAction, &QAction::triggered, this, &MainWindow::importGame);
  fileMenu->addSeparator();
  auto* quitAction = fileMenu->addAction(tr("Quit"));
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  auto* toolsMenu = menuBar()->addMenu(tr("Tools"));
  auto* settingsAction = toolsMenu->addAction(tr("Settings"));
  connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
  auto* helpMenu = menuBar()->addMenu(tr("Help"));
  auto* aboutAction = helpMenu->addAction(tr("About AcePS"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

  auto* header = addToolBar(tr("AcePS Header"));
  header->setMovable(false);
  header->setIconSize(QSize(22, 22));
  auto* brand = new QLabel(QStringLiteral("  A  AcePS"), this);
  brand->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #ffffff; padding: 4px 14px;"));
  header->addWidget(brand);
  auto* spacer = new QWidget(this);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  header->addWidget(spacer);
  searchBox_ = new QLineEdit(this);
  searchBox_->setPlaceholderText(tr("Search games"));
  searchBox_->setFixedWidth(220);
  header->addWidget(searchBox_);
  auto* settingsButton = new QToolButton(this);
  settingsButton->setText(QStringLiteral("⚙"));
  settingsButton->setToolTip(tr("Settings"));
  connect(settingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
  header->addWidget(settingsButton);
  importButton_ = new QToolButton(this);
  importButton_->setText(QStringLiteral("＋  Add game"));
  importButton_->setToolTip(tr("Import a game folder or build"));
  importButton_->setStyleSheet(QStringLiteral("QToolButton { background: #16a6f7; color: #ffffff; font-weight: 700; } QToolButton:hover { background: #42baff; }"));
  connect(importButton_, &QToolButton::clicked, this, &MainWindow::importGame);
  header->addWidget(importButton_);
}

void MainWindow::createLibraryView() {
  auto* container = new QWidget(this);
  auto* layout = new QVBoxLayout(container);
  layout->setContentsMargins(32, 22, 32, 16);
  layout->setSpacing(18);

  auto* nav = new QLabel(tr("GAMES"), container);
  nav->setStyleSheet(QStringLiteral("color: #53c4ff; font-size: 13px; font-weight: 800; letter-spacing: 2px;"));
  layout->addWidget(nav);

  auto* hero = new QWidget(container);
  hero->setMinimumHeight(220);
  hero->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #142b4a, stop:0.55 #102039, stop:1 #0e1727); border: 1px solid #223d60; border-radius: 16px;"));
  auto* heroLayout = new QHBoxLayout(hero);
  heroLayout->setContentsMargins(26, 22, 26, 22);
  heroArtwork_ = new QLabel(hero);
  heroArtwork_->setFixedSize(170, 170);
  heroArtwork_->setAlignment(Qt::AlignCenter);
  heroArtwork_->setStyleSheet(QStringLiteral("background: #0a1424; border-radius: 12px; color: #4d6b8d; font-size: 52px;"));
  heroArtwork_->setText(QStringLiteral("＋"));
  heroLayout->addWidget(heroArtwork_);
  auto* heroText = new QVBoxLayout();
  heroText->addStretch();
  heroTitle_ = new QLabel(tr("Your games, ready to play"), hero);
  heroTitle_->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700; color: #ffffff;"));
  heroText->addWidget(heroTitle_);
  heroSubtitle_ = new QLabel(tr("Import a PS4 game folder or build with the + Add game button."), hero);
  heroSubtitle_->setStyleSheet(QStringLiteral("font-size: 15px; color: #9cb1c9;"));
  heroText->addWidget(heroSubtitle_);
  heroText->addStretch();
  heroLayout->addLayout(heroText);
  heroLayout->addStretch();
  layout->addWidget(hero);

  auto* libraryTitle = new QLabel(tr("Game Library"), container);
  libraryTitle->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700; color: #ffffff;"));
  layout->addWidget(libraryTitle);
  gameList_ = new QListWidget(container);
  gameList_->setViewMode(QListView::IconMode);
  gameList_->setResizeMode(QListView::Adjust);
  gameList_->setMovement(QListView::Static);
  gameList_->setSpacing(6);
  gameList_->setIconSize(QSize(150, 150));
  gameList_->setGridSize(QSize(190, 210));
  connect(gameList_, &QListWidget::currentRowChanged, this, &MainWindow::selectGame);
  layout->addWidget(gameList_, 1);
  setCentralWidget(container);
}

void MainWindow::refreshLibrary() {
  std::string error;
  if (!library_.refresh(error)) {
    statusLabel_->setText(tr("Library error: %1").arg(QString::fromStdString(error)));
    return;
  }
  gameList_->clear();
  for (const auto& game : library_.entries()) {
    auto* item = new QListWidgetItem(QString::fromStdString(game.displayName), gameList_);
    const auto iconPath = game.root / "sce_sys" / "icon0.png";
    if (std::filesystem::exists(iconPath)) item->setIcon(QIcon(QString::fromStdString(iconPath.string())));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    item->setToolTip(QString::fromStdString(game.root.string()));
  }
  if (library_.entries().empty()) {
    heroTitle_->setText(tr("Your games, ready to play"));
    heroSubtitle_->setText(tr("Import a PS4 game folder or build with the + Add game button."));
    statusLabel_->setText(tr("No games added yet"));
  } else {
    selectGame(0);
  }
}

void MainWindow::selectGame(int row) {
  if (row < 0 || row >= static_cast<int>(library_.entries().size())) return;
  updateHero(&library_.entries()[static_cast<std::size_t>(row)]);
}

void MainWindow::updateHero(const GameEntry* game) {
  if (game == nullptr) return;
  heroTitle_->setText(QString::fromStdString(game->displayName));
  heroSubtitle_->setText(tr("Ready to launch  •  %1").arg(QString::fromStdString(game->titleId)));
  const auto iconPath = game->root / "sce_sys" / "icon0.png";
  if (std::filesystem::exists(iconPath)) {
    heroArtwork_->setPixmap(QPixmap(QString::fromStdString(iconPath.string())).scaled(heroArtwork_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    heroArtwork_->setText({});
  }
}

void MainWindow::importGame() {
  const auto path = QFileDialog::getExistingDirectory(this, tr("Choose a PS4 game folder or build"));
  if (!path.isEmpty()) addImportedGame(std::filesystem::path(path.toStdString()));
}

void MainWindow::addImportedGame(const std::filesystem::path& path) {
  std::string error;
  if (!library_.addPath(path, error)) {
    QMessageBox::warning(this, tr("Could not add game"), QString::fromStdString(error));
    return;
  }
  refreshLibrary();
  statusLabel_->setText(tr("Added %1").arg(QString::fromStdString(path.filename().string())));
}

void MainWindow::showSettings() {
  QMessageBox::information(this, tr("Settings"), tr("Graphics, input, paths, and advanced settings are coming next."));
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(this, tr("About AcePS"), tr("AcePS %1\nA focused, original PS4 emulator project.").arg(QStringLiteral(ACEPS_VERSION)));
}

} // namespace aceps::app
