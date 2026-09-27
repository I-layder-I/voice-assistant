#include "vosk_api.h"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <alsa/asoundlib.h>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

constexpr int BUFFER_FRAMES = 4000;

atomic<bool> shutdownRequested = false;

void signalHandler(int);
struct CommandInfo;
struct Config {

  bool forceDefaultPaths = false;
  bool forceDefaultModelPath = false;
  bool forceDefaultCommandsPath = false;

  fs::path modelPath;
  fs::path commandsPath;

  int sampleRate = 16000;
};

void definePaths(Config &config);

class VoiceAssistantWorker {
public:
  explicit VoiceAssistantWorker(const Config &config)
      : modelPath(config.modelPath), commandsPath(config.commandsPath),
        sampleRate(config.sampleRate), running(false), model(nullptr),
        recognizer(nullptr), capture_handle(nullptr), alsa_initialized(false) {}

  ~VoiceAssistantWorker() { stop(); }
  bool executeCommandScript(const string &command_name);
  string extractTextFromJson(const string &json);
  vector<string> getFilesInDirectory(const fs::path &dir);
  string getFileExtension(const string &p);
  string getFilenameWithoutExtension(const string &p);
  vector<string> extractKeywordsFromScript(const fs::path &scriptPath);
  string findCommandForText(const string &text);
  void loadCommands();
  void start();
  void stop();
  void run();
  bool init();
  void loop();
  bool isRunning() const { return running.load(); }

  VoiceAssistantWorker &operator=(const VoiceAssistantWorker &) = delete;

  atomic<bool> running;

  VoskModel *model;
  VoskRecognizer *recognizer;

  fs::path modelPath;
  fs::path commandsPath;
  int sampleRate;

  vector<CommandInfo> commands;
  snd_pcm_t *capture_handle;
  bool alsa_initialized;
  thread t;
};

class VoiceAssistant {
  VoiceAssistantWorker worker;

public:
  bool isRunning() const { return worker.isRunning(); }

  explicit VoiceAssistant(const Config &config) : worker(config) {}

  void start() { worker.start(); }

  void stop() { worker.stop(); }

  ~VoiceAssistant() { stop(); }
};

int main(int argc, char *argv[]) {
  CLI::App app{"Voice Assistant"};

  Config config;
  bool debug = false;

  app.add_option("-m,--model", config.modelPath,
                 "Override the standard Model path");
  app.add_option("-c,--commands", config.commandsPath,
                 "Override the standard Commands path");
  app.add_option("-r,--sample-rate", config.sampleRate,
                 "Override the standard Sample Rate");
  app.add_flag("--vosk-debug", debug, "Enable Vosk debug logs");
  app.add_flag("--default-paths", config.forceDefaultPaths,
               "Use default installed Model and Commands paths");
  app.add_flag("--default-model", config.forceDefaultModelPath,
               "Use default installed Model path");
  app.add_flag("--default-commands", config.forceDefaultCommandsPath,
               "Use default installed Commands path");

  CLI11_PARSE(app, argc, argv);

  definePaths(config);

  if (debug) {
    std::cout << "Debug enabled\n";
    vosk_set_log_level(1);
  } else
    vosk_set_log_level(-1);

  VoiceAssistant a(config);

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  a.start();

  while (a.isRunning() && !shutdownRequested)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

  a.stop();
  return 0;
}

void signalHandler(int) { shutdownRequested = true; }

struct CommandInfo {
  string script_name;
  vector<string> keywords;
};

bool VoiceAssistantWorker::executeCommandScript(const string &command_name) {
  fs::path script_path = commandsPath / (command_name + ".sh");
  if (!fs::exists(script_path))
    return false;

  pid_t pid = fork();
  if (pid < 0)
    return false;

  if (pid == 0) {
    // первый дочерний
    setsid(); // новая сессия, отрыв от терминала

    pid_t pid2 = fork();
    if (pid2 < 0)
      exit(1);
    if (pid2 > 0)
      exit(0); // первый дочерний завершается

    // второй дочерний — полностью отвязан
    // закрыть стандартные дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    execl("/bin/bash", "bash", script_path.c_str(), nullptr);
    exit(1);
  }

  // родитель ждёт только первого fork, он завершается мгновенно
  waitpid(pid, nullptr, 0);
  return true;
}

string VoiceAssistantWorker::findCommandForText(const string &text) {
  string lower = text;
  transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  for (auto &cmd : commands) {
    for (auto &kw : cmd.keywords) {
      if (lower.find(kw) != string::npos)
        return cmd.script_name;
    }
  }
  return "";
}

string VoiceAssistantWorker::extractTextFromJson(const string &json) {
  size_t p = json.find("\"text\"");
  if (p == string::npos)
    return "";

  p = json.find(":", p);
  if (p == string::npos)
    return "";

  size_t s = json.find("\"", p);
  size_t e = json.find("\"", s + 1);

  if (s == string::npos || e == string::npos)
    return "";

  return json.substr(s + 1, e - s - 1);
}

vector<string> VoiceAssistantWorker::getFilesInDirectory(const fs::path &dir) {
  vector<string> files;

  if (!fs::is_directory(dir))
    return files;

  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file())
      files.push_back(entry.path().string());
  }

  return files;
}

string VoiceAssistantWorker::getFileExtension(const string &p) {
  size_t d = p.find_last_of('.');
  return (d == string::npos) ? "" : p.substr(d);
}

string VoiceAssistantWorker::getFilenameWithoutExtension(const string &p) {
  size_t s = p.find_last_of('/');
  size_t d = p.find_last_of('.');

  string name = (s == string::npos) ? p : p.substr(s + 1);
  if (d != string::npos && d > s)
    name = name.substr(0, d - s - 1);

  return name;
}

vector<string>
VoiceAssistantWorker::extractKeywordsFromScript(const fs::path &scriptPath) {
  vector<string> keys;
  ifstream file(scriptPath);
  if (!file)
    return keys;

  string line;
  while (getline(file, line)) {
    string marker = "# WORDS :";
    size_t pos = line.find(marker);
    if (pos == string::npos)
      continue;

    stringstream ss(line.substr(pos + marker.size()));
    string k;

    while (getline(ss, k, ',')) {
      k.erase(remove_if(k.begin(), k.end(), ::isspace), k.end());
      transform(k.begin(), k.end(), k.begin(), ::tolower);
      if (!k.empty())
        keys.push_back(k);
    }
    break;
  }
  return keys;
}

void VoiceAssistantWorker::loadCommands() {
  commands.clear();

  if (commandsPath.empty()) {
    cout << "Commands path not found\n";
    return;
  }

  if (!fs::is_directory(commandsPath)) {
    cout << "Invalid commands path: " << commandsPath << '\n';
    return;
  }

  for (const auto &file : getFilesInDirectory(commandsPath)) {
    if (getFileExtension(file) != ".sh")
      continue;

    CommandInfo cmd;
    cmd.script_name = getFilenameWithoutExtension(file);
    cmd.keywords = extractKeywordsFromScript(file);

    if (!cmd.keywords.empty()) {
      commands.push_back(cmd);
    }
  }

  cout << "Commands loaded: ";

  if (!commands.empty()) {
    cout << commands.front().script_name;

    for (size_t i = 1; i < commands.size(); ++i) {
      cout << ", " << commands[i].script_name;
    }
  }

  cout << '\n';
}

bool VoiceAssistantWorker::init() {
  if (modelPath.empty()) {
    cout << "Model path not found\n";
    return false;
  }

  if (!fs::is_directory(modelPath)) {
    cout << "Invalid model path: " << modelPath << '\n';
    return false;
  }

  model = vosk_model_new(modelPath.c_str());

  if (!model) {
    cout << "Failed to load Vosk model\n";
    return false;
  }

  recognizer = vosk_recognizer_new(model, sampleRate);

  if (!recognizer) {
    cout << "Failed to create Vosk recognizer\n";
    vosk_model_free(model);
    model = nullptr;
    return false;
  }

  loadCommands();

  return true;
}

void VoiceAssistantWorker::loop() {
  while (running) {
    run();
  }
}

void VoiceAssistantWorker::start() {
  if (running)
    return;

  if (!init())
    return;

  running = true;

  t = std::thread([this] { loop(); });
}

void VoiceAssistantWorker::stop() {
  running = false;

  if (t.joinable())
    t.join();

  if (capture_handle) {
    snd_pcm_close(capture_handle);
    capture_handle = nullptr;
  }

  alsa_initialized = false;

  if (recognizer) {
    vosk_recognizer_free(recognizer);
    recognizer = nullptr;
  }

  if (model) {
    vosk_model_free(model);
    model = nullptr;
  }
}

void VoiceAssistantWorker::run() {
  static vector<short> buffer(BUFFER_FRAMES);

  if (!alsa_initialized) {
    int err;

    if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE,
                            SND_PCM_NONBLOCK)) < 0) {
      cout << "ALSA error: " << snd_strerror(err) << "\n";
      running = false;
      return;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);

    snd_pcm_hw_params_any(capture_handle, params);
    snd_pcm_hw_params_set_access(capture_handle, params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, params, SND_PCM_FORMAT_S16_LE);

    unsigned int rate = sampleRate;

    if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, params, &rate,
                                               nullptr)) < 0) {
      cout << "ALSA error: " << snd_strerror(err) << "\n";
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
      running = false;
      return;
    }

    if (rate != static_cast<unsigned int>(sampleRate)) {
      cout << "Unsupported sample rate: " << rate << " Hz, expected "
           << sampleRate << " Hz\n";

      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
      running = false;
      return;
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle, params, 1)) < 0) {
      cout << "ALSA error: " << snd_strerror(err) << "\n";
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
      running = false;
      return;
    }

    if ((err = snd_pcm_hw_params(capture_handle, params)) < 0) {
      cout << "ALSA error: " << snd_strerror(err) << "\n";
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
      running = false;
      return;
    }

    alsa_initialized = true;
  }

  snd_pcm_sframes_t frames =
      snd_pcm_readi(capture_handle, buffer.data(), BUFFER_FRAMES);

  if (frames == -EAGAIN) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return;
  }

  if (frames < 0) {
    frames = snd_pcm_recover(capture_handle, frames, 0);
    return;
  }

  if (frames <= 0 || !recognizer)
    return;

  const char *data = reinterpret_cast<const char *>(buffer.data());
  int len = frames * sizeof(short);

  if (vosk_recognizer_accept_waveform(recognizer, data, len)) {
    string json = vosk_recognizer_result(recognizer);
    string text = extractTextFromJson(json);

    if (!text.empty()) {
      cout << "Recognized: " << text << "\n";

      string cmd = findCommandForText(text);
      if (!cmd.empty())
        executeCommandScript(cmd);
    }
  }
}

void definePaths(Config &config) {
  const char *home = getenv("HOME");

  if (!home) {
    cerr << "HOME environment variable is not set\n";
    return;
  }

  fs::path defaultModelPath =
      fs::path(home) / ".local/share/voice-assistant/model";

  fs::path defaultCommandsPath =
      fs::path(home) / ".config/voice-assistant/commands";

  fs::path localModelPath = "./model";
  fs::path localCommandsPath = "./commands";

  // Явно указанные --model / --commands имеют наивысший приоритет
  if (config.modelPath.empty()) {
    if (config.forceDefaultPaths || config.forceDefaultModelPath) {
      config.modelPath = defaultModelPath;
    } else if (fs::is_directory(localModelPath)) {
      config.modelPath = localModelPath;
    } else if (fs::is_directory(defaultModelPath)) {
      config.modelPath = defaultModelPath;
    }
  }

  if (config.commandsPath.empty()) {
    if (config.forceDefaultPaths || config.forceDefaultCommandsPath) {
      config.commandsPath = defaultCommandsPath;
    } else if (fs::is_directory(localCommandsPath)) {
      config.commandsPath = localCommandsPath;
    } else if (fs::is_directory(defaultCommandsPath)) {
      config.commandsPath = defaultCommandsPath;
    }
  }

  cout << "Model path: "
       << (config.modelPath.empty() ? "<not found>" : config.modelPath.string())
       << '\n';

  cout << "Commands path: "
       << (config.commandsPath.empty() ? "<not found>"
                                       : config.commandsPath.string())
       << '\n';
}
