// AcePS main-window declaration: defines the minimal game-library shell shown to desktop users.
#pragma once

#include <QMainWindow>

class QLabel;
class QListWidget;
class QStackedWidget;
class QTimer;

namespace AcePS::Interface {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void chooseGameLocation();
    void addGame();
    void playSelectedGame();
    void showSettings();
    void showAbout();
    void showGameContextMenu(const QPoint& position);
    void refreshFrameRate();

private:
    void buildMenuBar();
    void buildWelcomePage();
    void buildLibraryPage();
    void setStatusMessage(const QString& message);

    QStackedWidget* contentPages_ = nullptr;
    QListWidget* gameLibrary_ = nullptr;
    QLabel* frameRateLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* frameRateTimer_ = nullptr;
    int displayedFramesPerSecond_ = 0;
};

} // namespace AcePS::Interface
