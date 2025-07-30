#include "voiceassistant.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDateTime>
#include <QCoreApplication> // Для QCoreApplication::applicationDirPath()
#include <QFileInfo>       // Для QFileInfo::exists()
#include <QDebug>          // Для qDebug(), qWarning()
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include <iostream>

#define SAMPLE_RATE 16000
#define BUFFER_SIZE 8000

// --- Добавленная функция для поиска модели ---
std::string findModelPath() {
    qDebug() << "Начинаем поиск модели Vosk...";

    // 1. Проверить системный путь (если установлено системно)
    QString systemModelPath = "/usr/local/share/voice-assistant/model";
    if (QFileInfo::exists(systemModelPath)) {
        qDebug() << "Модель найдена в системной директории:" << systemModelPath;
        return systemModelPath.toStdString();
    } else {
        qDebug() << "Модель НЕ найдена в системной директории:" << systemModelPath;
    }

    // 2. Проверить путь относительно исполняемого файла
    QString appDir = QCoreApplication::applicationDirPath();
    QString localModelPath = appDir + "/model";
    if (QFileInfo::exists(localModelPath)) {
        qDebug() << "Модель найдена рядом с исполняемым файлом:" << localModelPath;
        return localModelPath.toStdString();
    } else {
        qDebug() << "Модель НЕ найдена рядом с исполняемым файлом:" << localModelPath;
    }

    // 3. Проверить относительный путь (текущая директория)
    QString relativeModelPath = "model";
    if (QFileInfo::exists(relativeModelPath)) {
        qDebug() << "Модель найдена в текущей директории:" << relativeModelPath;
        return relativeModelPath.toStdString();
    } else {
        qDebug() << "Модель НЕ найдена в текущей директории:" << relativeModelPath;
    }

    // 4. Модель не найдена
    qWarning() << "Модель Vosk не найдена ни в одном из проверенных путей.";
    return ""; // или бросить исключение
}
// --- ---

VoiceAssistantWorker::VoiceAssistantWorker(QObject *parent)
    : QObject(parent)
    , running(false)
    , model(nullptr)
    , recognizer(nullptr)
    , processTimer(new QTimer(this))
{
    connect(processTimer, &QTimer::timeout, this, &VoiceAssistantWorker::run);
}

VoiceAssistantWorker::~VoiceAssistantWorker()
{
    stop();
}

void VoiceAssistantWorker::start()
{
    if (running) return;
    
    emit logMessage("Поиск и загрузка модели Vosk...");
    
    // Используем новую функцию для поиска модели
    std::string modelPath = findModelPath();
    if (modelPath.empty()) {
        QString errorMsg = "Критическая ошибка: модель Vosk не найдена ни в одном из стандартных путей!";
        emit logMessage(errorMsg);
        // В worker'е лучше не показывать QMessageBox напрямую.
        // Лучше отправить сигнал в MainWindow.
        return;
    }

    emit logMessage(QString("Загрузка модели Vosk из: %1").arg(QString::fromStdString(modelPath)));
    
    // Используем найденный путь
    model = vosk_model_new(modelPath.c_str());
    if (!model) {
        QString errorMsg = QString("Ошибка загрузки модели Vosk из пути: %1").arg(QString::fromStdString(modelPath));
        emit logMessage(errorMsg);
        return;
    }
    
    recognizer = vosk_recognizer_new(model, SAMPLE_RATE);
    if (!recognizer) {
        emit logMessage("Ошибка создания распознавателя!");
        vosk_model_free(model);
        model = nullptr;
        return;
    }
    
    // Загружаем команды
    loadCommands();
    
    running = true;
    emit statusChanged(true);
    emit logMessage("Голосовой ассистент запущен");
    
    // Запускаем обработку аудио
    processTimer->start(100); // Обрабатываем каждые 100 мс
}

void VoiceAssistantWorker::stop()
{
    if (!running) return;
    
    processTimer->stop();
    running = false;
    emit statusChanged(false);
    emit logMessage("Голосовой ассистент остановлен");
    
    if (recognizer) {
        vosk_recognizer_free(recognizer);
        recognizer = nullptr;
    }
    
    if (model) {
        vosk_model_free(model);
        model = nullptr;
    }
}

void VoiceAssistantWorker::run()
{
    if (!running) return;
    
    static snd_pcm_t *capture_handle = nullptr;
    static snd_pcm_hw_params_t *hw_params = nullptr;
    static bool initialized = false;
    static std::vector<short> buffer(BUFFER_SIZE);
    
    // Инициализация ALSA (один раз)
    if (!initialized) {
        int err;
        
        if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
            emit logMessage(QString("Ошибка открытия аудио устройства: %1").arg(snd_strerror(err)));
            return;
        }
        
        snd_pcm_hw_params_alloca(&hw_params);
        snd_pcm_hw_params_any(capture_handle, hw_params);
        snd_pcm_hw_params_set_access(capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
        snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, (unsigned int[]){SAMPLE_RATE}, 0);
        snd_pcm_hw_params_set_channels(capture_handle, hw_params, 1);
        
        if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0) {
            emit logMessage(QString("Ошибка настройки аудио параметров: %1").arg(snd_strerror(err)));
            snd_pcm_close(capture_handle);
            return;
        }
        
        initialized = true;
        emit logMessage("Аудио устройство инициализировано");
    }
    
    // Чтение аудио данных
    snd_pcm_sframes_t frames = snd_pcm_readi(capture_handle, buffer.data(), BUFFER_SIZE/4);
    if (frames < 0) {
        frames = snd_pcm_recover(capture_handle, frames, 0);
        if (frames < 0) {
            emit logMessage(QString("Ошибка чтения аудио: %1").arg(snd_strerror(frames)));
            return;
        }
    }
    
    if (frames > 0 && running && recognizer) {
        const char* audio_data = reinterpret_cast<const char*>(buffer.data());
        int audio_length = frames * sizeof(short);
        
        if (vosk_recognizer_accept_waveform(recognizer, audio_data, audio_length)) {
            const char* result = vosk_recognizer_result(recognizer);
            std::string json_result(result);
            
            // Извлекаем текст из JSON
            std::string recognized_text = extractTextFromJson(json_result);
            
            if (!recognized_text.empty()) {
                emit logMessage(QString("Распознано: %1").arg(QString::fromStdString(recognized_text)));
                
                // Проверяем команды выхода
                std::string lower_text = recognized_text;
                std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
                if (lower_text.find("выход") != std::string::npos || 
                    lower_text.find("завершить") != std::string::npos) {
                    emit logMessage("Команда выхода распознана");
                    QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
                    return;
                }
                
                // Ищем подходящую команду
                std::string command_name = findCommandForText(recognized_text);
                
                if (!command_name.empty()) {
                    if (executeCommandScript(command_name)) {
                        emit logMessage(QString("Выполнена команда: %1").arg(QString::fromStdString(command_name)));
                    }
                } else {
                    emit logMessage("Команда не распознана");
                }
            }
        }
    }
}

std::string VoiceAssistantWorker::extractTextFromJson(const std::string& json_result)
{
    // Ищем "text" :
    size_t text_pos = json_result.find("\"text\"");
    if (text_pos != std::string::npos) {
        size_t colon_pos = json_result.find(":", text_pos);
        if (colon_pos != std::string::npos) {
            size_t start_quote = json_result.find("\"", colon_pos);
            if (start_quote != std::string::npos) {
                size_t end_quote = json_result.find("\"", start_quote + 1);
                if (end_quote != std::string::npos) {
                    return json_result.substr(start_quote + 1, end_quote - start_quote - 1);
                }
            }
        }
    }
    
    return "";
}

bool VoiceAssistantWorker::fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::vector<std::string> VoiceAssistantWorker::getFilesInDirectory(const std::string& dir_path) {
    std::vector<std::string> files;
    DIR* dir = opendir(dir_path.c_str());
    
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename != "." && filename != "..") {
                files.push_back(dir_path + "/" + filename);
            }
        }
        closedir(dir);
    }
    
    return files;
}

std::string VoiceAssistantWorker::getFilenameWithoutExtension(const std::string& filepath) {
    size_t last_slash = filepath.find_last_of("/");
    size_t last_dot = filepath.find_last_of(".");
    
    std::string filename = filepath;
    if (last_slash != std::string::npos) {
        filename = filepath.substr(last_slash + 1);
    }
    
    if (last_dot != std::string::npos && last_dot > (last_slash + 1)) {
        filename = filename.substr(0, last_dot - (last_slash + 1));
    }
    
    return filename;
}

std::string VoiceAssistantWorker::getFileExtension(const std::string& filepath) {
    size_t last_dot = filepath.find_last_of(".");
    if (last_dot != std::string::npos) {
        return filepath.substr(last_dot);
    }
    return "";
}

std::vector<std::string> VoiceAssistantWorker::extractKeywordsFromScript(const std::string& script_path) {
    std::vector<std::string> keywords;
    std::ifstream file(script_path);
    
    if (!file.is_open()) {
        return keywords;
    }
    
    std::string line;
    // Читаем первые несколько строк в поисках комментария с WORDS
    for (int i = 0; i < 10 && std::getline(file, line); i++) {  // Проверяем первые 10 строк
        // Ищем комментарий с WORDS
        size_t pos = line.find("# WORDS :");
        if (pos != std::string::npos) {
            std::string keywords_line = line.substr(pos + 9);  // Пропускаем "# WORDS :"
            
            // Разделяем ключевые слова по запятым
            std::stringstream ss(keywords_line);
            std::string keyword;
            while (std::getline(ss, keyword, ',')) {
                // Удаляем пробелы в начале и конце
                keyword.erase(0, keyword.find_first_not_of(" \t"));
                keyword.erase(keyword.find_last_not_of(" \t") + 1);
                if (!keyword.empty()) {
                    // Приводим к нижнему регистру
                    std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
                    keywords.push_back(keyword);
                }
            }
            break;
        }
    }
    
    file.close();
    return keywords;
}

void VoiceAssistantWorker::loadCommands() 
{
    commands.clear();

    // Определяем путь к директории команд и присваиваем значение переменной-члену класса
    if (QCoreApplication::applicationFilePath() == "/usr/local/bin/voice-assistant") {
        this->ComPath = (QDir::homePath() + "/.config/voice-assistant/commands").toStdString();
    } else {
        this->ComPath = "commands";
    }

    // Проверяем существование директории (std::string передается напрямую)
    if (!fileExists(this->ComPath)) {
        // Создаем директорию commands
        // mkdir требует const char*, используем .c_str()
        int mkdir_result = mkdir(this->ComPath.c_str(), 0755);
        if (mkdir_result == 0) {
             // Для logMessage, который, судя по ошибкам, принимает QString,
             // преобразуем std::string в QString
             emit logMessage(QString("Создана директория %1").arg(QString::fromStdString(this->ComPath)));
             // Или так: emit logMessage("Создана директория " + QString::fromStdString(this->ComPath));
        } else {
             emit logMessage(QString("Не удалось создать директорию %1").arg(QString::fromStdString(this->ComPath)));
             // Или так: emit logMessage("Не удалось создать директорию " + QString::fromStdString(this->ComPath));
        }
        emit logMessage("Поместите сюда bash скрипты с ключевыми словами в комментариях");
        return;
    }

    // Получаем список файлов в директории commands (std::string передается напрямую)
    std::vector<std::string> files = getFilesInDirectory(this->ComPath);

    // Проходим по всем .sh файлам
    for (const auto& filepath : files) {
        // Проверяем расширение файла (предполагается, что getFileExtension принимает std::string)
        if (getFileExtension(filepath) == ".sh") {
            // Получаем имя скрипта без расширения (предполагается, что getFilenameWithoutExtension принимает std::string)
            std::string script_name = getFilenameWithoutExtension(filepath);
            // Извлекаем ключевые слова из скрипта (предполагается, что extractKeywordsFromScript принимает std::string)
            std::vector<std::string> keywords = extractKeywordsFromScript(filepath);
            if (!keywords.empty()) {
                // Создаем структуру CommandInfo и заполняем её
                CommandInfo cmd_info;
                cmd_info.script_name = script_name;
                cmd_info.keywords = keywords;
                commands.push_back(cmd_info);

                // Формируем строку с ключевыми словами для лога
                QString keywords_str;
                for (size_t i = 0; i < keywords.size(); ++i) {
                    keywords_str += QString::fromStdString(keywords[i]);
                    if (i < keywords.size() - 1) keywords_str += ", ";
                }
                // Логируем загруженную команду
                emit logMessage(QString("Загружена команда: %1 (%2)")
                               .arg(QString::fromStdString(script_name))
                               .arg(keywords_str));
            }
        }
    }
}

std::string VoiceAssistantWorker::findCommandForText(const std::string& recognized_text) {
    std::string lower_text = recognized_text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    // Ищем совпадение по ключевым словам
    for (const auto& cmd : commands) {
        for (const auto& keyword : cmd.keywords) {
            if (lower_text.find(keyword) != std::string::npos) {
                return cmd.script_name;
            }
        }
    }
    
    return "";
}

bool VoiceAssistantWorker::executeCommandScript(const std::string& command_name) {
    std::string script_path = this->ComPath + "/" + command_name + ".sh";
    
    if (fileExists(script_path)) {
        emit logMessage(QString("Выполняю скрипт: %1").arg(QString::fromStdString(script_path)));
        
        // Выполняем скрипт
        int result = system(script_path.c_str());
        if (result == 0) {
            emit logMessage("Скрипт выполнен успешно");
            return true;
        } else {
            emit logMessage("Ошибка выполнения скрипта");
            return false;
        }
    } else {
        emit logMessage(QString("Скрипт не найден: %1").arg(QString::fromStdString(script_path)));
        return false;
    }
}

VoiceAssistant::VoiceAssistant(QObject *parent)
    : QObject(parent)
    , workerThread(new QThread(this))
    , worker(new VoiceAssistantWorker)
{
    worker->moveToThread(workerThread);
    
    connect(worker, &VoiceAssistantWorker::logMessage, this, &VoiceAssistant::logMessage);
    connect(worker, &VoiceAssistantWorker::statusChanged, this, &VoiceAssistant::statusChanged);
    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    
    workerThread->start();
}

VoiceAssistant::~VoiceAssistant()
{
    workerThread->quit();
    workerThread->wait();
}

bool VoiceAssistant::isRunning() const
{
    return worker->isRunning();
}

void VoiceAssistant::start()
{
    QMetaObject::invokeMethod(worker, "start", Qt::QueuedConnection);
}

void VoiceAssistant::stop()
{
    QMetaObject::invokeMethod(worker, "stop", Qt::QueuedConnection);
}
