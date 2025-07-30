#ifndef VOICEASSISTANT_H
#define VOICEASSISTANT_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <alsa/asoundlib.h>
#include "vosk_api.h"
#include <vector>
#include <string>

struct CommandInfo {
    std::string script_name;
    std::vector<std::string> keywords;
};

class VoiceAssistantWorker : public QObject
{
    Q_OBJECT

public:
    explicit VoiceAssistantWorker(QObject *parent = nullptr);
    ~VoiceAssistantWorker();
    
    bool isRunning() const { return running; }

public slots:
    void start();
    void stop();

signals:
    void logMessage(const QString& message);
    void statusChanged(bool running);

private:
    void run();
    void loadCommands();
    std::string findCommandForText(const std::string& text);
    bool executeCommandScript(const std::string& command_name);
    std::vector<std::string> extractKeywordsFromScript(const std::string& script_path);
    std::vector<std::string> getFilesInDirectory(const std::string& dir_path);
    std::string getFilenameWithoutExtension(const std::string& filepath);
    std::string getFileExtension(const std::string& filepath);
    bool fileExists(const std::string& path);
    std::string extractTextFromJson(const std::string& json_result);
    
    // --- Объявление функции для поиска модели УДАЛЕНО из класса ---
    // std::string findModelPath(); // <--- Эта строка должна быть удалена
    // --- ---

    bool running;
    VoskModel *model;
    VoskRecognizer *recognizer;
    std::vector<CommandInfo> commands;
    QTimer *processTimer;
    std::string ComPath;
};

// --- Добавляем объявление свободной функции ВНЕ класса ---
std::string findModelPath();
// --- ---

class VoiceAssistant : public QObject
{
    Q_OBJECT

public:
    explicit VoiceAssistant(QObject *parent = nullptr);
    ~VoiceAssistant();

    bool isRunning() const;
    
public slots:
    void start();
    void stop();

signals:
    void logMessage(const QString& message);
    void statusChanged(bool running);

private:
    QThread *workerThread;
    VoiceAssistantWorker *worker;
};

#endif // VOICEASSISTANT_H
