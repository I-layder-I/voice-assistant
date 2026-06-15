#include "vosk_api.h"
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

#define SAMPLE_RATE 16000
#define BUFFER_SIZE 8000

string findModelPath();
void signalHandler(int);
struct CommandInfo;

class VoiceAssistantWorker {
public:
  VoiceAssistantWorker()
      : running(false), model(nullptr), recognizer(nullptr),
        capture_handle(nullptr), alsa_initialized(false) {}

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
  void init();
  void loop();

private:
  VoiceAssistantWorker(const VoiceAssistantWorker &) = delete;
  VoiceAssistantWorker &operator=(const VoiceAssistantWorker &) = delete;

  atomic<bool> running;

  VoskModel *model;
  VoskRecognizer *recognizer;

  vector<CommandInfo> commands;
  fs::path ComPath;
  snd_pcm_t *capture_handle;
  bool alsa_initialized;
  thread t;
};

class VoiceAssistant {
  VoiceAssistantWorker worker;

public:
  void start() { worker.start(); }

  void stop() { worker.stop(); }

  ~VoiceAssistant() { stop(); }
};

VoiceAssistant *g_assistant = nullptr;

int main() {
  VoiceAssistant a;
  g_assistant = &a;
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  a.start();
  cout << "Press Enter to exit...\n";
  cin.get();
  a.stop();
}

string findModelPath() {
  cout << "Searching for the Vosk model...\n";

  string systemModelPath = "/usr/local/share/voice-assistant/model";
  if (fs::exists(systemModelPath)) {
    cout << "Model found in the system directory: " << systemModelPath << "\n";
    return systemModelPath;
  }

  if (fs::exists("model")) {
    cout << "Model found in the current directory\n";
    return "model";
  }

  cout << "Vosk model not found\n";
  return "";
}

void signalHandler(int) {
  if (g_assistant)
    g_assistant->stop();
  exit(0);
}

struct CommandInfo {
  string script_name;
  vector<string> keywords;
};

bool VoiceAssistantWorker::executeCommandScript(const string &command_name) {
  fs::path script_path = ComPath / (command_name + ".sh");
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
  if (!fs::exists(dir))
    return files;

  for (auto &e : fs::directory_iterator(dir))
    files.push_back(e.path().string());

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

  char buf[4096];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  const char *home = getenv("HOME");
  if (!home)
    home = "/tmp";
  if (len != -1) {
    buf[len] = '\0';
    fs::path exePath(buf);
    if (exePath == "/usr/local/bin/voice-assistant") {
      ComPath = fs::path(home) / ".config/voice-assistant/commands";
    } else {
      ComPath = "commands";
    }

    if (!fs::exists(ComPath)) {
      fs::create_directories(ComPath);
      cout << "directory created " << ComPath.string() << "\n";
      return;
    }

    for (auto &file : getFilesInDirectory(ComPath)) {
      if (getFileExtension(file) == ".sh") {
        CommandInfo cmd;
        cmd.script_name = getFilenameWithoutExtension(file);
        cmd.keywords = extractKeywordsFromScript(file);

        if (!cmd.keywords.empty()) {
          commands.push_back(cmd);
          cout << "Command loaded: " << cmd.script_name << "\n";
        }
      }
    }
  }
}

void VoiceAssistantWorker::init() {
  string modelPath = findModelPath();
  if (modelPath.empty())
    return;

  model = vosk_model_new(modelPath.c_str());
  if (!model)
    return;

  recognizer = vosk_recognizer_new(model, SAMPLE_RATE);
  if (!recognizer)
    return;

  loadCommands();
}

void VoiceAssistantWorker::loop() {
  while (running) {
    run();
    this_thread::sleep_for(chrono::milliseconds(50));
  }
}

void VoiceAssistantWorker::start() {
  if (running)
    return;

  init();

  if (!model || !recognizer)
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
  if (!running)
    return;

  static vector<short> buffer(BUFFER_SIZE);

  if (!alsa_initialized) {
    int err;

    if ((err = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE,
                            0)) < 0) {
      cout << "ALSA error: " << snd_strerror(err) << "\n";
      return;
    }

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);

    snd_pcm_hw_params_any(capture_handle, params);
    snd_pcm_hw_params_set_access(capture_handle, params,
                                 SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture_handle, params, SND_PCM_FORMAT_S16_LE);

    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(capture_handle, params, &rate, nullptr);
    snd_pcm_hw_params_set_channels(capture_handle, params, 1);

    if ((err = snd_pcm_hw_params(capture_handle, params)) < 0) {
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
      return;
    }
    alsa_initialized = true;
  }

  snd_pcm_sframes_t frames =
      snd_pcm_readi(capture_handle, buffer.data(), BUFFER_SIZE / 2);

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
