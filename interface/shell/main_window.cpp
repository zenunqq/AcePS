// AcePS main-window implementation: provides a friendly, non-emulation GUI skeleton and safe placeholder actions.
#include "interface/shell/main_window.h"

#include <QAction>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace AcePS::Interface {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AcePS");
    resize(1080, 700);
    buildMenuBar();

    contentPages_ = new QStackedWidget(this);
    buildWelcomePage();
    buildLibraryPage();
    setCentralWidget(contentPages_);

    frameRateLabel_ = new QLabel("FPS: --", this);
    statusLabel_ = new QLabel("Ready", this);
    statusBar()->addPermanentWidget(frameRateLabel_);
    statusBar()->addWidget(statusLabel_, 1);

    frameRateTimer_ = new QTimer(this);
    connect(frameRateTimer_, &QTimer::timeout, this, &MainWindow::refreshFrameRate);
    frameRateTimer_->start(1000);
}

void MainWindow::buildMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Add Game...", this, &MainWindow::addGame);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close);

    QMenu* settingsMenu = menuBar()->addMenu("&Settings");
    settingsMenu->addAction("Preferences...", this, &MainWindow::showSettings);

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About AcePS", this, &MainWindow::showAbout);
}

void MainWindow::buildWelcomePage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);

    auto* heading = new QLabel("Welcome to AcePS", page);
    heading->setStyleSheet("font-size: 28px; font-weight: bold;");
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);

    auto* prompt = new QLabel("Where are your PS4 games stored?", page);
    prompt->setAlignment(Qt::AlignCenter);
    layout->addWidget(prompt);

    auto* browseButton = new QPushButton("Browse for games", page);
    browseButton->setMaximumWidth(180);
    connect(browseButton, &QPushButton::clicked, this, &MainWindow::chooseGameLocation);
    layout->addWidget(browseButton, 0, Qt::AlignHCenter);
    contentPages_->addWidget(page);
}

void MainWindow::buildLibraryPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* addButton = new QPushButton("Add Game", page);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addGame);
    layout->addWidget(addButton, 0, Qt::AlignLeft);

    gameLibrary_ = new QListWidget(page);
    gameLibrary_->setViewMode(QListView::IconMode);
    gameLibrary_->setIconSize(QSize(120, 170));
    gameLibrary_->setGridSize(QSize(160, 220));
    gameLibrary_->setResizeMode(QListView::Adjust);
    gameLibrary_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(gameLibrary_, &QListWidget::customContextMenuRequested, this, &MainWindow::showGameContextMenu);
    layout->addWidget(gameLibrary_);

    auto* playButton = new QPushButton("Play", page);
    connect(playButton, &QPushButton::clicked, this, &MainWindow::playSelectedGame);
    layout->addWidget(playButton, 0, Qt::AlignRight);
    contentPages_->addWidget(page);
}

void MainWindow::chooseGameLocation() {
    const QString gameDirectory = QFileDialog::getExistingDirectory(this, "Choose your PS4 games folder");
    if (gameDirectory.isEmpty()) {
        return;
    }
    contentPages_->setCurrentIndex(1);
    setStatusMessage("Game folder selected: " + gameDirectory);
}

void MainWindow::addGame() {
    const QString gamePath = QFileDialog::getOpenFileName(this, "Add a game", {}, "PS4 content (*.pkg);;All files (*)");
    if (gamePath.isEmpty()) {
        return;
    }
    gameLibrary_->addItem(QFileInfo(gamePath).fileName());
    contentPages_->setCurrentIndex(1);
    setStatusMessage("Added game placeholder: " + gamePath);
}

void MainWindow::playSelectedGame() {
    if (gameLibrary_->currentItem() == nullptr) {
        setStatusMessage("Select a game before pressing Play.");
        return;
    }
    setStatusMessage("Launch requested for " + gameLibrary_->currentItem()->text() + ". Emulator runtime is not implemented yet.");
}

void MainWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("AcePS Settings");
    auto* tabs = new QTabWidget(&dialog);
    for (const QString& tabName : {"General", "Graphics", "Input", "Paths"}) {
        auto* tab = new QWidget(tabs);
        auto* form = new QFormLayout(tab);
        form->addRow(new QLabel(tabName + " settings will be available here as their subsystem is implemented.", tab));
        tabs->addTab(tab, tabName);
    }
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(tabs);
    dialog.resize(560, 300);
    dialog.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About AcePS", "AcePS is an original experimental PS4 emulator project.\n\nThis build currently provides only the desktop-shell skeleton.");
}

void MainWindow::showGameContextMenu(const QPoint& position) {
    if (gameLibrary_->itemAt(position) == nullptr) {
        return;
    }
    QMenu menu(this);
    menu.addAction("View game info", this, [this] { setStatusMessage("Game info is not available until metadata loading is implemented."); });
    menu.addAction("Open save folder", this, [this] { setStatusMessage("Save-data support is not implemented yet."); });
    menu.addAction("Remove from library", this, [this] { delete gameLibrary_->takeItem(gameLibrary_->currentRow()); });
    menu.exec(gameLibrary_->viewport()->mapToGlobal(position));
}

void MainWindow::refreshFrameRate() {
    displayedFramesPerSecond_ = 0;
    frameRateLabel_->setText(QString("FPS: %1").arg(displayedFramesPerSecond_));
}

void MainWindow::setStatusMessage(const QString& message) {
    statusLabel_->setText(message);
}

} // namespace AcePS::Interface
