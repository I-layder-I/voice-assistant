#include "vosk_api.h"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <alsa/asoundlib.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unicode/normalizer2.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <unistd.h>
#include <unordered_map>
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
  bool stdinMode = false;
  string matching = "substring";

  string language = "ru";
};

void defineCommands(Config &config);

struct VoskModelDeleter {
  void operator()(VoskModel *model) const {
    if (model)
      vosk_model_free(model);
  }
};

struct VoskRecognizerDeleter {
  void operator()(VoskRecognizer *recognizer) const {
    if (recognizer)
      vosk_recognizer_free(recognizer);
  }
};

struct PcmDeleter {
  void operator()(snd_pcm_t *handle) const {
    if (handle)
      snd_pcm_close(handle);
  }
};

using PcmPtr = unique_ptr<snd_pcm_t, PcmDeleter>;

using VoskModelPtr = unique_ptr<VoskModel, VoskModelDeleter>;

using VoskRecognizerPtr = unique_ptr<VoskRecognizer, VoskRecognizerDeleter>;

class VoiceAssistantWorker {
public:
  explicit VoiceAssistantWorker(const Config &config)
      : modelPath(config.modelPath), commandsPath(config.commandsPath),
        sampleRate(config.sampleRate), stdinMode(config.stdinMode),
        matching(config.matching), running(false) {}

  ~VoiceAssistantWorker() { stop(); }

  void setModelPath(const fs::path &path) { modelPath = path; }

  bool start();
  void stop();
  bool isRunning() const { return running.load(); }

private:
  bool executeCommandScript(const string &command_name);
  string extractTextFromJson(const string &json);
  vector<fs::path> getShFiles(const fs::path &dir);
  vector<string> extractKeywordsFromScript(const fs::path &scriptPath);
  string findCommandForText(const string &text);
  string normalizeText(const string &text);
  vector<string> splitWords(const string &text);
  bool containsWordSequence(const vector<string> &text,
                            const vector<string> &keyword);
  void processText(const string &text);
  void stdinLoop();
  string trim(const string &text);
  bool loadCommands();
  void run();
  bool init();
  bool initVosk();
  void loop();

  VoiceAssistantWorker &operator=(const VoiceAssistantWorker &) = delete;

  atomic<bool> running;

  VoskModelPtr model;
  VoskRecognizerPtr recognizer;

  fs::path modelPath;
  fs::path commandsPath;
  int sampleRate;
  bool stdinMode;
  string matching;

  vector<CommandInfo> commands;

  PcmPtr capture_handle;

  thread t;
};

class ModelManager {
public:
  explicit ModelManager(const Config &config)
      : language(config.language), modelPath(config.modelPath),
        forceDefaultPaths(config.forceDefaultPaths),
        forceDefaultModelPath(config.forceDefaultModelPath) {}

  void defineModel();

  const fs::path &getModelPath() const { return modelPath; }

private:
  string language;

  fs::path modelPath;

  bool forceDefaultPaths;
  bool forceDefaultModelPath;
};

class VoiceAssistant {
  ModelManager manager;
  VoiceAssistantWorker worker;

public:
  bool isRunning() const { return worker.isRunning(); }

  explicit VoiceAssistant(const Config &config)
      : manager(config), worker(config) {
    manager.defineModel();
    worker.setModelPath(manager.getModelPath());
  }

  bool start() { return worker.start(); }

  void stop() { worker.stop(); }

  ~VoiceAssistant() { stop(); }
};

int main(int argc, char *argv[]) {
  CLI::App app{"Voice Assistant"};

  Config config;
  bool debug = false;

  app.add_option("-m,--model", config.modelPath, "Set the Model path");
  app.add_option("-c,--commands", config.commandsPath, "Set the Commands path");
  app.add_option("-r,--sample-rate", config.sampleRate, "Set the Sample Rate");
  app.add_flag("--stdin", config.stdinMode,
               "Read commands from standard input");
  app.add_option("--matching", config.matching, "Set the Matching")
      ->check(CLI::IsMember({"exact", "substring"}));
  app.add_flag("--vosk-debug", debug, "Enable Vosk debug logs");
  app.add_flag("--default-paths", config.forceDefaultPaths,
               "Use the default Model and Commands paths");
  app.add_flag("--default-model", config.forceDefaultModelPath,
               "Use the default Model path");
  app.add_flag("--default-commands", config.forceDefaultCommandsPath,
               "Use the default Commands path");
  app.add_option("--language", config.language, "Set the language")
      ->check(CLI::IsMember({"ru", "en"}));

  CLI11_PARSE(app, argc, argv);

  defineCommands(config);

  if (debug) {
    cout << "Debug enabled\n";
    vosk_set_log_level(1);
  } else
    vosk_set_log_level(-1);

  VoiceAssistant a(config);

  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  if (!a.start())
    return 1;

  while (a.isRunning() && !shutdownRequested)
    this_thread::sleep_for(chrono::milliseconds(100));

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

  int pipefd[2];

  if (pipe(pipefd) < 0)
    return false;

  int flags = fcntl(pipefd[1], F_GETFD);

  if (flags < 0 || fcntl(pipefd[1], F_SETFD, flags | FD_CLOEXEC) < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  pid_t pid = fork();

  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {
    close(pipefd[0]);

    if (setsid() < 0) {
      int err = errno;

      if (write(pipefd[1], &err, sizeof(err)) != sizeof(err))
        _exit(1);

      _exit(1);
    }

    pid_t pid2 = fork();

    if (pid2 < 0) {
      int err = errno;

      if (write(pipefd[1], &err, sizeof(err)) != sizeof(err))
        _exit(1);

      _exit(1);
    }

    if (pid2 > 0) {
      // First child no longer needs the pipe.
      close(pipefd[1]);
      _exit(0);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    execl("/bin/bash", "bash", script_path.c_str(), nullptr);

    int err = errno;
    if (write(pipefd[1], &err, sizeof(err)) != sizeof(err))
      _exit(1);

    _exit(1);
  }

  close(pipefd[1]);

  int status;
  pid_t result;

  do {
    result = waitpid(pid, &status, 0);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    close(pipefd[0]);
    return false;
  }

  int exec_errno = 0;
  ssize_t bytes = read(pipefd[0], &exec_errno, sizeof(exec_errno));

  close(pipefd[0]);

  if (bytes > 0) {
    cerr << "Failed to execute command '" << command_name
         << "': " << strerror(exec_errno) << '\n';
    return false;
  }

  return true;
}

vector<string> VoiceAssistantWorker::splitWords(const string &text) {
  vector<string> words;
  stringstream ss(text);
  string word;

  while (ss >> word)
    words.push_back(word);

  return words;
}

bool VoiceAssistantWorker::containsWordSequence(const vector<string> &text,
                                                const vector<string> &keyword) {

  if (keyword.empty() || keyword.size() > text.size())
    return false;

  for (size_t i = 0; i <= text.size() - keyword.size(); ++i) {
    bool match = true;

    for (size_t j = 0; j < keyword.size(); ++j) {
      if (text[i + j] != keyword[j]) {
        match = false;
        break;
      }
    }

    if (match) {
      return true;
    }
  }

  return false;
}

string VoiceAssistantWorker::findCommandForText(const string &text) {
  string normalizedText = normalizeText(text);

  vector<string> textWords = splitWords(normalizedText);

  if (textWords.empty())
    return "";

  // exact
  if (matching == "exact") {
    for (const auto &cmd : commands) {
      for (const auto &kw : cmd.keywords) {
        vector<string> keywordWords = splitWords(kw);

        if (keywordWords == textWords)
          return cmd.script_name;
      }
    }

    return "";
  }

  // substring
  string bestCommand;
  size_t bestKeywordWords = 0;

  for (const auto &cmd : commands) {
    for (const auto &kw : cmd.keywords) {
      vector<string> keywordWords = splitWords(kw);

      if (!containsWordSequence(textWords, keywordWords))
        continue;

      if (keywordWords.size() > bestKeywordWords) {
        bestKeywordWords = keywordWords.size();
        bestCommand = cmd.script_name;
      }
    }
  }

  return bestCommand;
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

vector<fs::path> VoiceAssistantWorker::getShFiles(const fs::path &dir) {
  vector<fs::path> files;

  if (!fs::is_directory(dir))
    return files;

  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".sh")
      files.push_back(entry.path());
  }

  return files;
}

vector<string>
VoiceAssistantWorker::extractKeywordsFromScript(const fs::path &scriptPath) {
  vector<string> keys;

  ifstream file(scriptPath);

  if (!file)
    return keys;

  string line;

  while (getline(file, line)) {
    line = normalizeText(line);

    auto markerEnd = line.find(':');

    if (markerEnd == string::npos)
      continue;

    string marker = line.substr(0, markerEnd);

    marker.erase(remove_if(marker.begin(), marker.end(),
                           [](unsigned char c) { return isspace(c); }),
                 marker.end());

    if (marker != "#words")
      continue;

    stringstream ss(line.substr(markerEnd + 1));
    string keyword;

    while (getline(ss, keyword, ',')) {
      keyword = trim(keyword);

      if (!keyword.empty())
        keys.push_back(keyword);
    }

    break;
  }

  return keys;
}

string VoiceAssistantWorker::normalizeText(const string &text) {
  UErrorCode status = U_ZERO_ERROR;

  const icu::Normalizer2 *normalizer = icu::Normalizer2::getNFCInstance(status);

  if (U_FAILURE(status))
    return text;

  icu::UnicodeString unicode = icu::UnicodeString::fromUTF8(text);

  unicode.toLower();

  icu::UnicodeString normalized = normalizer->normalize(unicode, status);

  if (U_FAILURE(status))
    return text;

  string result;
  normalized.toUTF8String(result);

  return result;
}

string VoiceAssistantWorker::trim(const string &text) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == string::npos)
    return "";

  size_t end = text.find_last_not_of(" \t\r\n");

  return text.substr(start, end - start + 1);
}

void VoiceAssistantWorker::processText(const string &text) {
  if (text.empty())
    return;

  cout << "Recognized: " << text << '\n';

  string cmd = findCommandForText(text);

  if (cmd.empty())
    return;

  cout << "Executing: " << cmd << endl;

  if (!executeCommandScript(cmd)) {
    cerr << "Failed to execute command: " << cmd << '\n';
  }
}

void VoiceAssistantWorker::stdinLoop() {
  string text;

  while (running) {
    pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    int result = poll(&pfd, 1, 100);

    if (result < 0) {
      if (errno == EINTR)
        continue;

      break;
    }

    if (result == 0)
      continue;

    if (pfd.revents & POLLIN) {
      if (!getline(cin, text))
        break;

      processText(text);
    }

    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
      break;
  }

  running = false;
}

bool VoiceAssistantWorker::loadCommands() {
  commands.clear();

  if (!fs::exists(commandsPath) || !fs::is_directory(commandsPath)) {
    cerr << "Commands directory does not exist: " << commandsPath << '\n';
    return false;
  }

  unordered_map<string, string> keywordOwners;

  for (const auto &path : getShFiles(commandsPath)) {

    CommandInfo cmd;

    cmd.script_name = path.stem().string();
    cmd.keywords = extractKeywordsFromScript(path);

    if (cmd.keywords.empty()) {
      cerr << "Warning: no keywords in " << cmd.script_name << '\n';
      continue;
    }

    for (const auto &keyword : cmd.keywords) {
      auto [it, inserted] = keywordOwners.emplace(keyword, cmd.script_name);

      if (!inserted) {
        cerr << "Error: duplicate keyword: " << keyword << '\n';

        cerr << "  First command: " << it->second << '\n';

        cerr << "  Second command: " << cmd.script_name << '\n';

        return false;
      }
    }

    commands.push_back(std::move(cmd));
  }

  cout << "Commands loaded: ";

  if (!commands.empty()) {
    cout << commands.front().script_name;

    for (size_t i = 1; i < commands.size(); ++i) {
      cout << ", " << commands[i].script_name;
    }
  }

  cout << '\n';

  return true;
}

bool VoiceAssistantWorker::init() {
  if (!loadCommands())
    return false;

  if (stdinMode)
    return true;

  return initVosk();
}

bool VoiceAssistantWorker::initVosk() {

  if (modelPath.empty()) {
    cerr << "Model path not found\n";
    return false;
  }

  if (!fs::is_directory(modelPath)) {
    cerr << "Invalid model path: " << modelPath << '\n';
    return false;
  }

  model.reset(vosk_model_new(modelPath.c_str()));

  if (!model) {
    cerr << "Failed to load Vosk model\n";
    return false;
  }

  recognizer.reset(vosk_recognizer_new(model.get(), sampleRate));

  if (!recognizer) {
    cerr << "Failed to create Vosk recognizer\n";
    return false;
  }

  return true;
}

void VoiceAssistantWorker::loop() {
  while (running) {
    run();
  }
}

bool VoiceAssistantWorker::start() {
  if (running)
    return true;

  if (!init())
    return false;

  running = true;

  if (stdinMode)
    t = thread([this] { stdinLoop(); });
  else
    t = thread([this] { loop(); });

  return true;
}

void VoiceAssistantWorker::stop() {
  running = false;

  if (t.joinable())
    t.join();

  capture_handle.reset();
  recognizer.reset();
  model.reset();
}

void VoiceAssistantWorker::run() {
  static vector<short> buffer(BUFFER_FRAMES);

  if (!capture_handle) {
    int err;
    snd_pcm_t *handle = nullptr;

    if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE,
                            SND_PCM_NONBLOCK)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      running = false;
      return;
    }

    capture_handle.reset(handle);

    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);

    if ((err = snd_pcm_hw_params_any(capture_handle.get(), params)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }
    if ((err = snd_pcm_hw_params_set_access(capture_handle.get(), params,
                                            SND_PCM_ACCESS_RW_INTERLEAVED)) <
        0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }
    if ((err = snd_pcm_hw_params_set_format(capture_handle.get(), params,
                                            SND_PCM_FORMAT_S16_LE)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }

    unsigned int rate = sampleRate;

    if ((err = snd_pcm_hw_params_set_rate_near(capture_handle.get(), params,
                                               &rate, nullptr)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }

    if (rate != static_cast<unsigned int>(sampleRate)) {
      cerr << "Unsupported sample rate: " << rate << " Hz, expected "
           << sampleRate << " Hz\n";

      capture_handle.reset();
      running = false;
      return;
    }

    if ((err = snd_pcm_hw_params_set_channels(capture_handle.get(), params,
                                              1)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }

    if ((err = snd_pcm_hw_params(capture_handle.get(), params)) < 0) {
      cerr << "ALSA error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
      return;
    }
  }

  snd_pcm_sframes_t frames =
      snd_pcm_readi(capture_handle.get(), buffer.data(), BUFFER_FRAMES);

  if (frames == -EAGAIN) {
    this_thread::sleep_for(chrono::milliseconds(10));
    return;
  }

  if (frames < 0) {
    int err = snd_pcm_recover(capture_handle.get(), frames, 0);

    if (err < 0) {
      cerr << "ALSA recovery error: " << snd_strerror(err) << "\n";
      capture_handle.reset();
      running = false;
    }

    return;
  }

  if (!running)
    return;

  if (frames <= 0 || !recognizer)
    return;

  const char *data = reinterpret_cast<const char *>(buffer.data());
  int len = frames * sizeof(short);

  if (vosk_recognizer_accept_waveform(recognizer.get(), data, len)) {
    string json = vosk_recognizer_result(recognizer.get());
    string text = extractTextFromJson(json);
    processText(text);
  }
}

void defineCommands(Config &config) {
  const char *home = getenv("HOME");

  if (!home) {
    cerr << "HOME environment variable is not set\n";
    return;
  }

  fs::path defaultCommandsPath =
      fs::path(home) / ".config/voice-assistant/commands";

  fs::path localCommandsPath = "./commands";

  if (config.commandsPath.empty()) {
    if (config.forceDefaultPaths || config.forceDefaultCommandsPath) {
      config.commandsPath = defaultCommandsPath;
    } else if (fs::is_directory(localCommandsPath)) {
      config.commandsPath = localCommandsPath;
    } else if (fs::is_directory(defaultCommandsPath)) {
      config.commandsPath = defaultCommandsPath;
    }
  }

  cout << "Commands path: "
       << (config.commandsPath.empty() ? "<not found>"
                                       : config.commandsPath.string())
       << '\n';
}

void ModelManager::defineModel() {
  const char *home = getenv("HOME");

  if (!home) {
    cerr << "HOME environment variable is not set\n";
    return;
  }

  fs::path defaultModelPath =
      fs::path(home) / ".local/share/voice-assistant/models" / language;

  fs::path localModelPath = fs::path("./models") / language;

  if (modelPath.empty()) {
    if (forceDefaultPaths || forceDefaultModelPath) {
      modelPath = defaultModelPath;
    } else if (fs::is_directory(localModelPath)) {
      modelPath = localModelPath;
    } else if (fs::is_directory(defaultModelPath)) {
      modelPath = defaultModelPath;
    }
  }
  cout << "Model path: "
       << (modelPath.empty() ? "<not found>" : modelPath.string()) << '\n';
}
