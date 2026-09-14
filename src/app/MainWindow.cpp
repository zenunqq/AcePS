/*
 * MainWindow.cpp implements AcePS's focused Games workspace. The layout uses a
 * compact navigation rail, a clear content column, restrained surfaces, and
 * Qt's native icon set so it feels like a maintained desktop application.
 */
#include "aceps/app/MainWindow.h"

#include "aceps/core/BootSequence.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#if defined(__linux__)
#include <X11/Xlib.h>
#endif
#include <QWidget>

namespace aceps::app {
namespace {
QLabel* textLabel(const QString& text, QWidget* parent, const QString& style) {
  auto* label = new QLabel(text, parent);
  label->setStyleSheet(style);
  return label;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), library_(std::filesystem::current_path() / ".aceps" / "games") {
  setWindowTitle(QStringLiteral("AcePS — Games"));
  setMinimumSize(1080, 680);
  setStyleSheet(QStringLiteral(R"(
    QMainWindow { background: #0b111b; color: #edf3fb; }
    QWidget { color: #edf3fb; }
    QLabel { background: transparent; }
    QMenuBar { background: #0b111b; color: #9aa9bd; padding: 5px 14px; border-bottom: 1px solid #202c3d; }
    QMenuBar::item:selected { background: #172335; color: #ffffff; border-radius: 4px; }
    QToolButton { background: transparent; border: 0; border-radius: 6px; padding: 8px 10px; color: #9caec2; }
    QToolButton:hover { background: #182638; color: #ffffff; }
    QLineEdit { background: #111c2b; border: 1px solid #26384e; border-radius: 7px; padding: 8px 12px; color: #ffffff; selection-background-color: #2f9ce8; }
    QLineEdit:focus { border: 1px solid #42a9ed; }
    QListWidget { background: transparent; border: 0; outline: 0; }
    QListWidget::item { background: #111c2b; border: 1px solid #1f3045; border-radius: 8px; margin: 6px; padding: 7px; }
    QListWidget::item:hover { background: #17283c; border-color: #365b7e; }
    QListWidget::item:selected { background: #162d46; border: 1px solid #49aef0; }
    QStatusBar { background: #080d15; color: #71839a; border-top: 1px solid #1a2737; }
  )"));
  createHeader();
  createLibraryView();
  statusLabel_ = new QLabel(tr("No games added yet"), this);
  statusBar()->addWidget(statusLabel_);
  refreshLibrary();
}

void MainWindow::createHeader() {
  auto* fileMenu = menuBar()->addMenu(tr("File"));
  auto* importAction = fileMenu->addAction(tr("Add game folder..."));
  connect(importAction, &QAction::triggered, this, &MainWindow::importGame);
  fileMenu->addSeparator();
  connect(fileMenu->addAction(tr("Quit")), &QAction::triggered, this, &QWidget::close);
  auto* toolsMenu = menuBar()->addMenu(tr("Tools"));
  connect(toolsMenu->addAction(tr("Settings")), &QAction::triggered, this, &MainWindow::showSettings);
  auto* helpMenu = menuBar()->addMenu(tr("Help"));
  connect(helpMenu->addAction(tr("About AcePS")), &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::createLibraryView() {
  auto* root = new QWidget(this);
  auto* rootLayout = new QHBoxLayout(root);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto* rail = new QFrame(root);
  rail->setObjectName(QStringLiteral("navigationRail"));
  rail->setFixedWidth(218);
  rail->setStyleSheet(QStringLiteral("#navigationRail { background: #0e1724; border-right: 1px solid #202d3e; }"));
  auto* railLayout = new QVBoxLayout(rail);
  railLayout->setContentsMargins(22, 24, 18, 20);
  railLayout->setSpacing(8);
  auto* brand = textLabel(QStringLiteral("A  <b>AcePS</b>"), rail,
                          QStringLiteral("font-size: 20px; color: #ffffff; padding: 4px 0 28px 2px;"));
  railLayout->addWidget(brand);
  auto* workspace = textLabel(tr("WORKSPACE"), rail,
                              QStringLiteral("font-size: 10px; font-weight: 800; letter-spacing: 1px; color: #60758e; padding: 0 0 8px 10px;"));
  railLayout->addWidget(workspace);
  auto* gamesButton = new QPushButton(style()->standardIcon(QStyle::SP_DirHomeIcon), tr("  Games"), rail);
  gamesButton->setEnabled(false);
  gamesButton->setStyleSheet(QStringLiteral("QPushButton { text-align: left; border: 0; border-radius: 6px; padding: 11px 12px; background: #1b3854; color: #ffffff; font-weight: 700; }"));
  railLayout->addWidget(gamesButton);
  auto* libraryButton = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogListView), tr("  Library"), rail);
  libraryButton->setStyleSheet(QStringLiteral("QPushButton { text-align: left; border: 0; border-radius: 6px; padding: 11px 12px; background: transparent; color: #93a5ba; } QPushButton:hover { background: #172638; color: #ffffff; }"));
  railLayout->addWidget(libraryButton);
  railLayout->addStretch();
  auto* buildLabel = textLabel(tr("AcePS 0.2\nVulkan backend"), rail,
                               QStringLiteral("font-size: 11px; color: #60758e; line-height: 1.5; padding-left: 10px;"));
  railLayout->addWidget(buildLabel);
  rootLayout->addWidget(rail);

  auto* content = new QWidget(root);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(34, 24, 38, 16);
  layout->setSpacing(16);
  auto* topRow = new QHBoxLayout();
  topRow->setSpacing(10);
  auto* heading = textLabel(tr("Games"), content,
                            QStringLiteral("font-size: 26px; font-weight: 700; color: #ffffff;"));
  topRow->addWidget(heading);
  topRow->addStretch();
  searchBox_ = new QLineEdit(content);
  searchBox_->setPlaceholderText(tr("Search library"));
  searchBox_->setClearButtonEnabled(true);
  searchBox_->setFixedWidth(220);
  topRow->addWidget(searchBox_);
  auto* refreshButton = new QToolButton(content);
  refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
  refreshButton->setToolTip(tr("Refresh library"));
  connect(refreshButton, &QToolButton::clicked, this, &MainWindow::refreshLibrary);
  topRow->addWidget(refreshButton);
  auto* settingsButton = new QToolButton(content);
  settingsButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  settingsButton->setToolTip(tr("Settings"));
  connect(settingsButton, &QToolButton::clicked, this, &MainWindow::showSettings);
  topRow->addWidget(settingsButton);
  importButton_ = new QToolButton(content);
  importButton_->setText(tr("Add game"));
  importButton_->setToolTip(tr("Import a game folder or build"));
  importButton_->setStyleSheet(QStringLiteral("QToolButton { background: #2b9ee8; border: 0; border-radius: 6px; padding: 9px 13px; color: #ffffff; font-weight: 700; } QToolButton:hover { background: #42b2f4; }"));
  connect(importButton_, &QToolButton::clicked, this, &MainWindow::importGame);
  topRow->addWidget(importButton_);
  layout->addLayout(topRow);

  auto* accent = new QFrame(content);
  accent->setFixedHeight(2);
  accent->setStyleSheet(QStringLiteral("background: #2b9ee8; border: 0;"));
  layout->addWidget(accent);
  auto* hero = new QFrame(content);
  hero->setStyleSheet(QStringLiteral("QFrame { background: #121f31; border: 1px solid #243b54; border-radius: 10px; }"));
  hero->setMinimumHeight(210);
  auto* heroLayout = new QHBoxLayout(hero);
  heroLayout->setContentsMargins(22, 20, 24, 20);
  heroLayout->setSpacing(20);
  heroArtwork_ = new QLabel(hero);
  heroArtwork_->setFixedSize(158, 158);
  heroArtwork_->setAlignment(Qt::AlignCenter);
  heroArtwork_->setStyleSheet(QStringLiteral("background: #0a1421; border: 1px solid #29425d; border-radius: 8px; color: #6f87a2; font-size: 13px;"));
  heroArtwork_->setText(tr("No artwork"));
  heroLayout->addWidget(heroArtwork_);
  auto* heroCopy = new QVBoxLayout();
  heroCopy->setSpacing(7);
  heroCopy->addStretch();
  heroTitle_ = new QLabel(tr("Games"), hero);
  heroTitle_->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: 0; font-size: 27px; font-weight: 700; color: #ffffff; }"));
  heroCopy->addWidget(heroTitle_);
  heroSubtitle_ = new QLabel(tr("Add a game folder or build to begin."), hero);
  heroSubtitle_->setWordWrap(true);
  heroSubtitle_->setStyleSheet(QStringLiteral("QLabel { background: transparent; border: 0; font-size: 14px; color: #94a9bf; }"));
  heroCopy->addWidget(heroSubtitle_);
  bootButton_ = new QPushButton(tr("Boot"), hero);
  bootButton_->setEnabled(false);
  bootButton_->setStyleSheet(QStringLiteral("QPushButton { background: #2b9ee8; border: 0; border-radius: 6px; padding: 9px 18px; color: #ffffff; font-weight: 700; } QPushButton:hover { background: #42b2f4; } QPushButton:disabled { background: #29435b; color: #7890a6; }"));
  connect(bootButton_, &QPushButton::clicked, this, &MainWindow::bootSelectedGame);
  heroCopy->addWidget(bootButton_, 0, Qt::AlignLeft);
  heroCopy->addStretch();
  heroLayout->addLayout(heroCopy, 1);
  layout->addWidget(hero);

  auto* sectionRow = new QHBoxLayout();
  auto* libraryTitle = textLabel(tr("Games"), content,
                                 QStringLiteral("font-size: 18px; font-weight: 700; color: #ffffff;"));
  sectionRow->addWidget(libraryTitle);
  sectionRow->addStretch();
  auto* count = textLabel(tr("Local library"), content,
                          QStringLiteral("font-size: 12px; color: #71869e;"));
  sectionRow->addWidget(count);
  layout->addLayout(sectionRow);
  emptyState_ = new QLabel(tr("No games added yet\n\nUse Add game to import a build or folder."), content);
  emptyState_->setAlignment(Qt::AlignCenter);
  emptyState_->setMinimumHeight(130);
  emptyState_->setStyleSheet(QStringLiteral("background: #0e1928; border: 1px dashed #2b435d; border-radius: 8px; color: #8096ad; font-size: 14px;"));
  layout->addWidget(emptyState_);
  gameList_ = new QListWidget(content);
  gameList_->setViewMode(QListView::IconMode);
  gameList_->setResizeMode(QListView::Adjust);
  gameList_->setMovement(QListView::Static);
  gameList_->setSpacing(4);
  gameList_->setIconSize(QSize(148, 148));
  gameList_->setGridSize(QSize(184, 204));
  connect(gameList_, &QListWidget::currentRowChanged, this, &MainWindow::selectGame);
  layout->addWidget(gameList_, 1);
  rootLayout->addWidget(content, 1);
  setCentralWidget(root);
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
  const bool empty = library_.entries().empty();
  emptyState_->setVisible(empty);
  gameList_->setVisible(!empty);
  if (empty) {
    bootButton_->setEnabled(false);
    heroTitle_->setText(tr("Games"));
    heroSubtitle_->setText(tr("Add a game folder or build to begin."));
    statusLabel_->setText(tr("No games added yet"));
  } else {
    selectGame(0);
    statusLabel_->setText(tr("%1 game(s) in library").arg(library_.entries().size()));
  }
}

void MainWindow::selectGame(int row) {
  if (row < 0 || row >= static_cast<int>(library_.entries().size())) return;
  updateHero(&library_.entries()[static_cast<std::size_t>(row)]);
}

void MainWindow::updateHero(const GameEntry* game) {
  if (game == nullptr) return;
  bootButton_->setEnabled(true);
  heroTitle_->setText(QString::fromStdString(game->displayName));
  heroSubtitle_->setText(tr("Ready to launch — %1").arg(QString::fromStdString(game->titleId)));
  const auto iconPath = game->root / "sce_sys" / "icon0.png";
  if (std::filesystem::exists(iconPath)) {
    heroArtwork_->setPixmap(QPixmap(QString::fromStdString(iconPath.string())).scaled(heroArtwork_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    heroArtwork_->setText({});
  }
}

void MainWindow::importGame() {
  const auto pkgPath = QFileDialog::getOpenFileName(this, tr("Choose a PS4 PKG or game folder"), {},
                                                    tr("PS4 packages (*.pkg);;All files (*)"));
  if (!pkgPath.isEmpty()) {
    addImportedGame(std::filesystem::path(pkgPath.toStdString()));
    return;
  }
  const auto folderPath = QFileDialog::getExistingDirectory(this, tr("Choose a PS4 game folder or build"));
  if (!folderPath.isEmpty()) addImportedGame(std::filesystem::path(folderPath.toStdString()));
}

void MainWindow::bootSelectedGame() {
  const auto row = gameList_->currentRow();
  if (row < 0 || row >= static_cast<int>(library_.entries().size())) {
    QMessageBox::information(this, tr("No game selected"), tr("Select a game before pressing Boot."));
    return;
  }

  const auto& game = library_.entries()[static_cast<std::size_t>(row)];
  auto elfPath = game.root / "eboot.bin";
  if (!std::filesystem::is_regular_file(elfPath)) {
    const auto app0Path = game.root / "app0" / "eboot.bin";
    if (std::filesystem::is_regular_file(app0Path)) elfPath = app0Path;
  }

  QDialog logDialog(this);
  logDialog.setWindowTitle(tr("Boot log — %1").arg(QString::fromStdString(game.displayName)));
  logDialog.resize(720, 440);
  auto* dialogLayout = new QVBoxLayout(&logDialog);
  auto* logView = new QPlainTextEdit(&logDialog);
  logView->setReadOnly(true);
  logView->setStyleSheet(QStringLiteral("QPlainTextEdit { background: #080d15; color: #b9d3e8; border: 1px solid #26384e; font-family: monospace; }"));
  dialogLayout->addWidget(logView, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &logDialog);
  connect(buttons, &QDialogButtonBox::rejected, &logDialog, &QDialog::reject);
  dialogLayout->addWidget(buttons);
  const auto appendLog = [logView](const QString& message) {
    logView->appendPlainText(message);
    QApplication::processEvents();
  };

  appendLog(tr("Selected game: %1").arg(QString::fromStdString(game.displayName)));
  appendLog(tr("ELF path: %1").arg(QString::fromStdString(elfPath.string())));
  if (!std::filesystem::is_regular_file(elfPath)) {
    appendLog(tr("ERROR: eboot.bin was not found in the selected game folder."));
    statusLabel_->setText(tr("Boot failed: eboot.bin not found"));
    logDialog.exec();
    return;
  }

  bootButton_->setEnabled(false);
  statusLabel_->setText(tr("Booting %1...").arg(QString::fromStdString(game.displayName)));
  appendLog(tr("Starting BootSequence..."));
  core::BootSequence bootSequence;
  gpu::NativeWindowHandle nativeWindow;
  nativeWindow.window = windowHandle() == nullptr ? 0 : windowHandle()->winId();
#if defined(__linux__)
  Display* display = XOpenDisplay(nullptr);
  nativeWindow.display = display;
#endif
  std::string error;
  const bool succeeded = bootSequence.run(elfPath, error, &nativeWindow);
#if defined(__linux__)
  if (display != nullptr) XCloseDisplay(display);
#endif
  if (succeeded) {
    appendLog(tr("BootSequence completed successfully."));
    statusLabel_->setText(tr("Boot completed: %1").arg(QString::fromStdString(game.displayName)));
  } else {
    appendLog(tr("BootSequence failed: %1").arg(QString::fromStdString(error)));
    statusLabel_->setText(tr("Boot failed: %1").arg(QString::fromStdString(error)));
  }
  bootButton_->setEnabled(true);
  logDialog.exec();
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
