#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// --- Включения Qt для основного функционала GUI ---
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
// --- Включения Qt для работы с файловой системой и настройками ---
#include <QDir>
#include <QFile>
#include <QTextStream>
// --- Включения Qt для получения информации о приложении ---
#include <QCoreApplication> // Для QCoreApplication::applicationFilePath()
// --- ---

#include "voiceassistant.h" // Предполагается, что этот файл находится в той же директории или в путях поиска

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStopClicked();
    void onAutoStartToggled(bool checked);
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void updateStatus();
    void appendLog(const QString& message);
    void onClearLogClicked();
    // --- Новые слоты для установки/удаления ---
    void onInstallClicked();
    void onUninstallClicked();
    // --- ---

private:
    void createTrayIcon();
    void loadSettings();
    void saveSettings();
    void setupUI();

    // --- Добавленные/измененные приватные члены для автозапуска ---
    QString generateDesktopFileContent() const;
    const QString autostartFilePath = QDir::homePath() + "/.config/autostart/voice-assistant-gui.desktop";
    // const QString desktopExecPath = QCoreApplication::applicationFilePath(); // Удалено, путь генерируется динамически
    // --- ---
    
    // --- Методы для установки/удаления ---
    QString generateSystemDesktopFileContent(const QString& execPath) const;
    bool isInstalled() const;
    // --- ---

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    VoiceAssistant *voiceAssistant;
    QTimer *statusTimer;

    QPushButton *startStopButton;
    QTextEdit *logTextEdit;
    QCheckBox *autoStartCheckBox; // Этот чекбокс теперь управляет и автозапуском приложения
    QLabel *statusLabel;
    QAction *startStopAction;
    QAction *quitAction;
    QAction *showAction;
    
    // --- Новые элементы UI ---
    QPushButton *installButton;
    QPushButton *uninstallButton;
    // --- ---
};

#endif // MAINWINDOW_H
