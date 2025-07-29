#include "mainwindow.h"
// --- Включения Qt для реализации ---
#include <QApplication>
#include <QCloseEvent>
#include <QSettings>
#include <QDateTime>
#include <QHBoxLayout>
#include <QStyle>
#include <QMessageBox>
#include <QProcess> // Для запуска команд sudo
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
// --- ---

// --- Конструктор ---
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , trayIcon(new QSystemTrayIcon(this))
    , voiceAssistant(new VoiceAssistant(this))
    , statusTimer(new QTimer(this))
    // Инициализация указателей на UI элементы
    , startStopButton(nullptr)
    , logTextEdit(nullptr)
    , autoStartCheckBox(nullptr)
    , statusLabel(nullptr)
    , startStopAction(nullptr)
    , quitAction(nullptr)
    , showAction(nullptr)
    , installButton(nullptr) // Инициализация новых кнопок
    , uninstallButton(nullptr)
{
    setupUI();
    createTrayIcon();
    loadSettings();

    connect(startStopButton, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(autoStartCheckBox, &QCheckBox::toggled, this, &MainWindow::onAutoStartToggled);
    connect(voiceAssistant, &VoiceAssistant::logMessage, this, &MainWindow::appendLog);
    connect(voiceAssistant, &VoiceAssistant::statusChanged, this, &MainWindow::updateStatus);
    connect(statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    connect(trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
    // connect для новых кнопок уже в setupUI

    statusTimer->start(1000);
    updateStatus();

    // Автозапуск функции прослушивания, если включено
    if (autoStartCheckBox->isChecked()) {
        QTimer::singleShot(1000, this, [this]() {
            voiceAssistant->start();
        });
    }
}
// --- ---

// --- Деструктор ---
MainWindow::~MainWindow()
{
    saveSettings();
}
// --- ---

// --- Обработчики событий ---
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Скрываем окно вместо закрытия
    hide();
    event->ignore();

    // Показываем уведомление
    trayIcon->showMessage("Голосовой Ассистент",
                         "Приложение работает в фоновом режиме",
                         QSystemTrayIcon::Information, 2000);
}
// --- ---

// --- Слоты ---
void MainWindow::onStartStopClicked()
{
    if (voiceAssistant->isRunning()) {
        voiceAssistant->stop();
    } else {
        voiceAssistant->start();
    }
}

void MainWindow::onAutoStartToggled(bool checked)
{
    saveSettings(); // Сохраняем состояние чекбокса

    if (checked) {
        // --- Включить автозапуск приложения ---
        // 1. Убедиться, что директория ~/.config/autostart существует
        QDir autostartDir(QDir::homePath() + "/.config");
        if (!autostartDir.exists("autostart")) {
            if (!autostartDir.mkdir("autostart")) {
                qWarning() << "Не удалось создать директорию ~/.config/autostart";
                appendLog("Ошибка: Не удалось создать директорию автозапуска.");
                autoStartCheckBox->setChecked(false);
                saveSettings();
                return;
            }
        }

        // 2. Создать или перезаписать файл .desktop
        QFile desktopFile(autostartFilePath);
        if (desktopFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&desktopFile);
            // Генерируем путь динамически - будет путь к ЗАПУЩЕННОМУ экземпляру
            out << generateDesktopFileContent();
            desktopFile.close();
            appendLog("Файл автозапуска добавлен: " + autostartFilePath);
        } else {
            qWarning() << "Не удалось записать файл автозапуска:" << autostartFilePath;
            appendLog("Ошибка: Не удалось создать файл автозапуска.");
            autoStartCheckBox->setChecked(false);
            saveSettings();
        }

    } else {
        // --- Отключить автозапуск приложения ---
        // 1. Попытаться удалить файл .desktop
        QFile desktopFile(autostartFilePath);
        if (desktopFile.exists()) {
            if (desktopFile.remove()) {
                appendLog("Файл автозапуска удален: " + autostartFilePath);
            } else {
                qWarning() << "Не удалось удалить файл автозапуска:" << autostartFilePath;
                appendLog("Ошибка: Не удалось удалить файл автозапуска.");
            }
        } else {
            appendLog("Файл автозапуска отсутствует, удаление не требуется.");
        }
    }
}


void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        show();
        raise();
        activateWindow();
    }
}

void MainWindow::updateStatus()
{
    if (voiceAssistant->isRunning()) {
        statusLabel->setText("Статус: Работает");
        statusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");
        startStopButton->setText("Остановить");
        startStopAction->setText("Остановить");
    } else {
        statusLabel->setText("Статус: Остановлен");
        statusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
        startStopButton->setText("Запустить");
        startStopAction->setText("Запустить");
    }
}

void MainWindow::appendLog(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    logTextEdit->append(QString("[%1] %2").arg(timestamp, message));

    // Прокручиваем вниз
    QTextCursor cursor = logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    logTextEdit->setTextCursor(cursor);
}

void MainWindow::onClearLogClicked()
{
    logTextEdit->clear();
}
// --- ---

// --- Приватные методы ---
void MainWindow::setupUI()
{
    setWindowTitle("Голосовой Ассистент");
    resize(600, 500); // Увеличим высоту для новых кнопок

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    // Статус
    statusLabel = new QLabel("Статус: Остановлен", this);
    statusLabel->setStyleSheet("QLabel { font-weight: bold; color: red; }");
    layout->addWidget(statusLabel);

    // Кнопка запуска/остановки
    startStopButton = new QPushButton("Запустить", this);
    startStopButton->setStyleSheet("QPushButton { font-size: 14px; padding: 10px; }");
    layout->addWidget(startStopButton);

    // Автозапуск
    autoStartCheckBox = new QCheckBox("Автозапуск приложения и функции прослушивания", this);
    layout->addWidget(autoStartCheckBox);

    // --- Новые кнопки установки/удаления ---
    QLabel *installLabel = new QLabel("Установка в систему:", this);
    layout->addWidget(installLabel);

    QHBoxLayout *installLayout = new QHBoxLayout();
    installButton = new QPushButton("Установить системно", this);
    uninstallButton = new QPushButton("Удалить из системы", this);
    installLayout->addWidget(installButton);
    installLayout->addWidget(uninstallButton);
    layout->addLayout(installLayout);

    // Обновим состояние кнопок установки при запуске
    bool installed = isInstalled();
    installButton->setEnabled(!installed);
    uninstallButton->setEnabled(installed);

    connect(installButton, &QPushButton::clicked, this, &MainWindow::onInstallClicked);
    connect(uninstallButton, &QPushButton::clicked, this, &MainWindow::onUninstallClicked);
    // --- ---

    // Лог
    QLabel *logLabel = new QLabel("Лог:", this);
    layout->addWidget(logLabel);

    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    layout->addWidget(logTextEdit);

    // Кнопка очистки лога
    QPushButton *clearLogButton = new QPushButton("Очистить лог", this);
    connect(clearLogButton, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);
    layout->addWidget(clearLogButton);
}
// --- ---

void MainWindow::createTrayIcon()
{
    trayIconMenu = new QMenu(this);
    startStopAction = new QAction("Запустить", this);
    connect(startStopAction, &QAction::triggered, this, &MainWindow::onStartStopClicked);

    showAction = new QAction("Показать", this);
    connect(showAction, &QAction::triggered, this, [this]() {
        show();
        raise();
        activateWindow();
    });

    quitAction = new QAction("Выход", this);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    trayIconMenu->addAction(startStopAction);
    trayIconMenu->addAction(showAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
    trayIcon->show();
}

void MainWindow::loadSettings()
{
    QSettings settings("VoiceAssistant", "GUI");
    // Значение по умолчанию false, как и просили
    autoStartCheckBox->setChecked(settings.value("autoStart", false).toBool());
    restoreGeometry(settings.value("geometry").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings settings("VoiceAssistant", "GUI");
    settings.setValue("autoStart", autoStartCheckBox->isChecked());
    settings.setValue("geometry", saveGeometry());
}

// --- Исправленный метод для автозапуска ---
// Генерирует .desktop файл с путем к ЗАПУЩЕННОМУ экземпляру приложения
QString MainWindow::generateDesktopFileContent() const {
    QString content =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Голосовой Ассистент GUI (Пользователь)\n" // Немного другое имя для различия
        "Comment=Голосовое управление системой (Автозапуск пользователя)\n"
        // Используем путь к ЗАПУЩЕННОМУ экземпляру приложения
        "Exec=" + QCoreApplication::applicationFilePath() + "\n"
        "Icon=audio-input-microphone\n"
        "Terminal=false\n"
        "Categories=AudioVideo;Audio;Recorder;\n"
        "X-GNOME-Autostart-enabled=true\n";

    return content;
}
// --- ---

// --- Новые методы для установки/удаления ---
QString MainWindow::generateSystemDesktopFileContent(const QString& execPath) const {
    QString content =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Голосовой Ассистент GUI\n"
        "Comment=Голосовое управление системой\n"
        "Exec=" + execPath + "\n" // Путь будет передаваться как аргумент
        "Icon=audio-input-microphone\n"
        "Terminal=false\n"
        "Categories=AudioVideo;Audio;Recorder;\n"
        "X-GNOME-Autostart-enabled=true\n";
    return content;
}

bool MainWindow::isInstalled() const {
    QFileInfo systemBinary("/usr/local/bin/voice_assistant_gui");
    QFileInfo systemDesktop("/usr/local/share/applications/voice-assistant-gui.desktop");
    return systemBinary.exists() && systemDesktop.exists();
}

void MainWindow::onInstallClicked() {
    QString program = "pkexec";
    QStringList arguments;

    QString scriptPath = QDir::tempPath() + "/install_voice_assistant.sh";
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось создать временный скрипт установки.");
        return;
    }

    QTextStream out(&scriptFile);
    out << "#!/bin/bash\n";
    out << "set -e\n";
    out << "echo \"=== Установка Голосового Ассистента ===\"\n";

    // --- Копирование исполняемого файла ---
    out << "echo \"Копирование исполняемого файла...\"\n";
    out << "cp \"" << QCoreApplication::applicationFilePath() << "\" /usr/local/bin/voice_assistant_gui\n";

    // --- Копирование библиотеки libvosk.so ---
    // Предполагаем, что libvosk.so находится рядом с запущенным исполняемым файлом
    QFileInfo appFileInfo(QCoreApplication::applicationFilePath());
    QString appDirPath = appFileInfo.absolutePath();
    out << "echo \"Копирование библиотеки libvosk.so...\"\n";
    out << "cp \"" << appDirPath << "/libvosk.so\" /usr/local/lib/\n";

    // --- Копирование модели ---
    out << "echo \"Создание директории для данных...\"\n";
    out << "mkdir -p /usr/local/share/voice-assistant\n";
    out << "echo \"Копирование модели Vosk...\"\n";
    out << "cp -r \"" << appDirPath << "/model\" /usr/local/share/voice-assistant/model_\n";
    out << "mv /usr/local/share/voice-assistant/model_ /usr/local/share/voice-assistant/model\n";
    out << "chmod -R 644 /usr/local/share/voice-assistant/model\n"; // Установка стандартных прав на файлы в модели

    // --- Установка прав ---
    out << "echo \"Установка прав на выполнение...\"\n";
    out << "chmod 755 /usr/local/bin/voice_assistant_gui\n";
    out << "chmod 644 /usr/local/lib/libvosk.so\n"; // Стандартные права для библиотеки

    // --- Обновление кэша библиотек ---
    out << "echo \"Обновление кэша библиотек...\"\n";
    out << "ldconfig\n";

    // --- Создание .desktop файла ---
    out << "echo \"Создание директории для .desktop файлов...\"\n";
    out << "mkdir -p /usr/local/share/applications\n";
    out << "echo \"Создание системного .desktop файла...\"\n";
    QString desktopContent = generateSystemDesktopFileContent("/usr/local/bin/voice_assistant_gui");
    QStringList lines = desktopContent.split('\n');
    out << "cat > /usr/local/share/applications/voice-assistant-gui.desktop << 'EOF'\n";
    for (const QString& line : lines) {
        if (!line.isEmpty()) {
            out << line << "\n";
        }
    }
    out << "EOF\n";
    out << "chmod 644 /usr/local/share/applications/voice-assistant-gui.desktop\n";

    out << "echo \"Установка завершена успешно!\"\n";
    out << "exit 0\n";
    scriptFile.close();

    QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    arguments << scriptPath;

    appendLog("Запуск установки с привилегиями...");
    QProcess *process = new QProcess(this);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process, scriptPath](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    appendLog("Установка завершена успешно.");
                    QMessageBox::information(this, "Успех", "Приложение успешно установлено системно.");
                    // Обновить состояние кнопок
                    installButton->setEnabled(false);
                    uninstallButton->setEnabled(true);
                } else {
                    QString errorOutput = process->readAllStandardError();
                    appendLog("Ошибка установки. Код: " + QString::number(exitCode));
                    if (!errorOutput.isEmpty()) {
                        appendLog("Ошибка: " + errorOutput);
                    }
                    QMessageBox::warning(this, "Ошибка", "Установка не удалась.\n" + errorOutput);
                }
                QFile::remove(scriptPath);
                process->deleteLater();
            });

    connect(process, &QProcess::errorOccurred, [this, process, scriptPath](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            appendLog("Ошибка: Не удалось запустить pkexec. Убедитесь, что он установлен.");
            QMessageBox::warning(this, "Ошибка", "Не удалось запустить pkexec. Установите пакет policykit-1 или аналогичный для вашей системы.");
        } else {
            appendLog("Ошибка процесса установки: " + QString::number(error));
            QMessageBox::warning(this, "Ошибка", "Произошла ошибка во время установки.");
        }
        QFile::remove(scriptPath);
        process->deleteLater();
    });

    process->start(program, arguments);

    if (!process->waitForStarted(5000)) {
         appendLog("Таймаут запуска процесса установки.");
         QMessageBox::warning(this, "Ошибка", "Таймаут запуска процесса установки.");
         QFile::remove(scriptPath);
         process->deleteLater();
    }
}

void MainWindow::onUninstallClicked() {
    int ret = QMessageBox::question(this, "Подтверждение",
        "Вы уверены, что хотите удалить Голосовой Ассистент из системы?\n"
        "Будут удалены:\n"
        "- /usr/local/bin/voice_assistant_gui\n"
        "- /usr/local/lib/libvosk.so\n"
        "- /usr/local/share/voice-assistant/model\n" // Добавлено
        "- /usr/local/share/applications/voice-assistant-gui.desktop",
        QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) {
        return;
    }

    QString program = "pkexec";
    QStringList arguments;

    QString scriptPath = QDir::tempPath() + "/uninstall_voice_assistant.sh";
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Ошибка", "Не удалось создать временный скрипт удаления.");
        return;
    }

    QTextStream out(&scriptFile);
    out << "#!/bin/bash\n";
    out << "set -e\n";
    out << "echo \"=== Удаление Голосового Ассистента ===\"\n";

    // --- Удаление файлов ---
    out << "echo \"Удаление исполняемого файла...\"\n";
    out << "rm -f /usr/local/bin/voice_assistant_gui\n";

    out << "echo \"Удаление библиотеки libvosk.so...\"\n";
    out << "rm -f /usr/local/lib/libvosk.so\n";

    out << "echo \"Удаление модели Vosk...\"\n"; // Добавлено
    out << "rm -rf /usr/local/share/voice-assistant\n"; // Добавлено

    out << "echo \"Обновление кэша библиотек...\"\n";
    out << "ldconfig\n";

    out << "echo \"Удаление .desktop файла...\"\n";
    out << "rm -f /usr/local/share/applications/voice-assistant-gui.desktop\n";

    out << "echo \"Удаление завершено успешно!\"\n";
    out << "exit 0\n";
    scriptFile.close();

    QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    arguments << scriptPath;

    appendLog("Запуск удаления с привилегиями...");
    QProcess *process = new QProcess(this);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, process, scriptPath](int exitCode, QProcess::ExitStatus exitStatus) {
                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    appendLog("Удаление завершено успешно.");
                    QMessageBox::information(this, "Успех", "Приложение успешно удалено из системы.");
                    // Обновить состояние кнопок
                    installButton->setEnabled(true);
                    uninstallButton->setEnabled(false);
                } else {
                    QString errorOutput = process->readAllStandardError();
                    appendLog("Ошибка удаления. Код: " + QString::number(exitCode));
                    if (!errorOutput.isEmpty()) {
                        appendLog("Ошибка: " + errorOutput);
                    }
                    QMessageBox::warning(this, "Ошибка", "Удаление не удалось.\n" + errorOutput);
                }
                QFile::remove(scriptPath);
                process->deleteLater();
            });

    connect(process, &QProcess::errorOccurred, [this, process, scriptPath](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            appendLog("Ошибка: Не удалось запустить pkexec для удаления.");
            QMessageBox::warning(this, "Ошибка", "Не удалось запустить pkexec для удаления.");
        } else {
            appendLog("Ошибка процесса удаления: " + QString::number(error));
            QMessageBox::warning(this, "Ошибка", "Произошла ошибка во время удаления.");
        }
        QFile::remove(scriptPath);
        process->deleteLater();
    });

    process->start(program, arguments);

    if (!process->waitForStarted(5000)) {
         appendLog("Таймаут запуска процесса удаления.");
         QMessageBox::warning(this, "Ошибка", "Таймаут запуска процесса удаления.");
         QFile::remove(scriptPath);
         process->deleteLater();
    }
}
// --- ---
