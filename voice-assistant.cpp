#include "vosk_api.h"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <alsa/asoundlib.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
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
struct CommandMatch {
  string script_name;
  size_t keyword_words = 0;
};
struct Config {
  bool forceDefaultPaths = false;
  bool forceDefaultModelPath = false;
  bool forceDefaultCommandsPath = false;
  bool debug = false;

  vector<fs::path> modelPaths;
  fs::path commandsPath;

  int sampleRate = 16000;
  bool stdinMode = false;
  string matching = "substring";

  vector<string> languages = {"en"};
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
      : modelPaths(config.modelPaths), commandsPath(config.commandsPath),
        sampleRate(config.sampleRate), stdinMode(config.stdinMode),
        matching(config.matching), debug(config.debug), running(false) {}

  ~VoiceAssistantWorker() { stop(); }

  void setModelPaths(const vector<fs::path> &paths) { modelPaths = paths; }

  bool start();
  void stop();
  bool isRunning() const { return running.load(); }

private:
  bool executeCommandScript(const string &command_name);
  string extractTextFromJson(const string &json);
  static double avgConfidenceFromJson(const string &json);
  static bool isSilence(const short *data, int frames);
  vector<fs::path> getShFiles(const fs::path &dir);
  vector<string> extractKeywordsFromScript(const fs::path &scriptPath);
  CommandMatch findCommandForText(const string &text);
  string normalizeText(const string &text);
  vector<string> splitWords(const string &text);
  bool containsWordSequence(const vector<string> &text,
                            const vector<string> &keyword);
  void processText(const string &text, const string &cmd);
  void stdinLoop();
  string trim(const string &text);
  bool loadCommands();
  void run();
  bool init();
  bool initVosk();
  void loop();

  VoiceAssistantWorker &operator=(const VoiceAssistantWorker &) = delete;

  atomic<bool> running;

  vector<VoskModelPtr> models;
  vector<VoskRecognizerPtr> recognizers;

  vector<fs::path> modelPaths;
  fs::path commandsPath;
  int sampleRate;
  bool stdinMode;
  string matching;
  bool debug;

  vector<CommandInfo> commands;

  chrono::steady_clock::time_point worker_start{};

  PcmPtr capture_handle;

  thread t;
};

class ModelManager {
public:
  explicit ModelManager(const Config &config)
      : languages(config.languages), modelPaths(config.modelPaths),
        forceDefaultPaths(config.forceDefaultPaths),
        forceDefaultModelPath(config.forceDefaultModelPath) {}

  void defineModel();

  const vector<fs::path> &getModelPaths() const { return modelPaths; }

private:
  vector<string> languages;

  vector<fs::path> modelPaths;

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
    worker.setModelPaths(manager.getModelPaths());
  }

  bool start() { return worker.start(); }

  void stop() { worker.stop(); }

  ~VoiceAssistant() { stop(); }
};

int main(int argc, char *argv[]) {
  CLI::App app{"Voice Assistant"};

  Config config;
  vector<fs::path> cliModels;
  vector<string> cliLanguages;

  app.add_option("-m,--model", cliModels,
                 "Set the Model(s) path(s); repeatable (--model "
                 "/path/to/model/ --model /path/)");
  app.add_option("-c,--commands", config.commandsPath, "Set the Commands path");
  app.add_option("-r,--sample-rate", config.sampleRate, "Set the Sample Rate");
  app.add_flag("--stdin", config.stdinMode,
               "Read commands from standard input");
  app.add_option("--matching", config.matching, "Set the Matching")
      ->check(CLI::IsMember({"exact", "substring"}));
  app.add_flag("--debug", config.debug, "Enable debug logs");
  app.add_flag("--default-paths", config.forceDefaultPaths,
               "Use the default Model and Commands paths");
  app.add_flag("--default-model", config.forceDefaultModelPath,
               "Use the default Model path");
  app.add_flag("--default-commands", config.forceDefaultCommandsPath,
               "Use the default Commands path");
  app.add_option(
         "--language", cliLanguages,
         "Set the language(s); repeatable (--language en --language ru)")
      ->check(CLI::IsMember({"en", "ru"}));

  CLI11_PARSE(app, argc, argv);

  if (!cliModels.empty())
    config.modelPaths = cliModels;

  if (!cliLanguages.empty())
    config.languages = cliLanguages;

  defineCommands(config);

  if (config.debug) {
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

CommandMatch VoiceAssistantWorker::findCommandForText(const string &text) {
  string normalizedText = normalizeText(text);
  vector<string> textWords = splitWords(normalizedText);

  if (textWords.empty())
    return {};

  CommandMatch best;
  size_t bestLen = 0;

  for (const auto &cmd : commands) {
    for (const auto &kw : cmd.keywords) {
      vector<string> keywordWords = splitWords(kw);

      bool ok = false;
      if (matching == "exact")
        ok = (keywordWords == textWords);
      else
        ok = containsWordSequence(textWords, keywordWords);

      if (!ok)
        continue;

      if (keywordWords.size() > bestLen) {
        bestLen = keywordWords.size();
        best = {cmd.script_name, bestLen};
      }
    }
  }

  return best;
}

string VoiceAssistantWorker::extractTextFromJson(const string &json) {
  size_t p = json.find("\"text\"");
  if (p == string::npos)
    return "";

  p = json.find(":", p);
  if (p == string::npos)
    return "";

  size_t s = json.find("\"", p);
  if (s == string::npos)
    return "";

  size_t e = json.find("\"", s + 1);
  if (e == string::npos)
    return "";

  return json.substr(s + 1, e - s - 1);
}

double VoiceAssistantWorker::avgConfidenceFromJson(const string &json) {
  double sum = 0.0;
  int n = 0;
  const string key = "\"conf\"";
  size_t pos = 0;

  while ((pos = json.find(key, pos)) != string::npos) {
    pos += key.size();

    size_t colon = json.find(':', pos);
    if (colon == string::npos)
      break;

    size_t start = json.find_first_of("-0123456789.", colon + 1);
    if (start == string::npos)
      break;

    size_t end = start;
    while (end < json.size()) {
      char c = json[end];
      if (isdigit((unsigned char)c) || c == '.' || c == '-' || c == '+' ||
          c == 'e' || c == 'E')
        ++end;
      else
        break;
    }

    try {
      sum += stod(json.substr(start, end - start));
      ++n;
    } catch (...) {
    }

    pos = end;
  }

  return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

bool VoiceAssistantWorker::isSilence(const short *data, int frames) {
  if (frames <= 0)
    return true;

  double sum = 0.0;
  for (int i = 0; i < frames; ++i) {
    double v = static_cast<double>(data[i]);
    sum += v * v;
  }

  double rms = std::sqrt(sum / frames);

  return rms < 500.0;
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

void VoiceAssistantWorker::processText(const string &text, const string &cmd) {
  if (text.empty() || cmd.empty())
    return;

  cout << "Recognized: " << text << '\n';
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

      CommandMatch m = findCommandForText(text);
      if (!m.script_name.empty())
        processText(text, m.script_name);
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
  if (modelPaths.empty()) {
    cerr << "No model paths configured\n";
    return false;
  }

  models.clear();
  recognizers.clear();

  for (const auto &path : modelPaths) {
    if (!fs::is_directory(path)) {
      cerr << "Invalid model path: " << path << '\n';
      return false;
    }

    VoskModelPtr m(vosk_model_new(path.c_str()));
    if (!m) {
      cerr << "Failed to load Vosk model: " << path << '\n';
      return false;
    }

    VoskRecognizerPtr r(vosk_recognizer_new(m.get(), sampleRate));
    if (!r) {
      cerr << "Failed to create recognizer for: " << path << '\n';
      return false;
    }

    vosk_recognizer_set_words(r.get(), 1);

    cout << "Model " << path << ": loaded\n";
    models.push_back(std::move(m));
    recognizers.push_back(std::move(r));
  }

  if (recognizers.empty()) {
    cerr << "No usable recognizers\n";
    return false;
  }

  return true;
}

void VoiceAssistantWorker::loop() {
  static auto last_trim = chrono::steady_clock::now();

  while (running) {
    run();

    auto now = chrono::steady_clock::now();

    if (now - last_trim > chrono::minutes(30)) {
      malloc_trim(0);
      last_trim = now;
    }

    if (now - worker_start > chrono::minutes(30)) {
      if (debug)
        cerr << "watchdog: recreating recognizers\n";

      recognizers.clear();

      for (auto &m : models) {
        VoskRecognizerPtr r(vosk_recognizer_new(m.get(), sampleRate));
        if (!r) {
          cerr << "watchdog: failed to recreate recognizer\n";
          continue;
        }
        vosk_recognizer_set_words(r.get(), 1);
        recognizers.push_back(std::move(r));
      }

      malloc_trim(0);
      worker_start = chrono::steady_clock::now();
    }
  }
}

bool VoiceAssistantWorker::start() {
  if (running)
    return true;

  if (!init())
    return false;

  running = true;
  worker_start = chrono::steady_clock::now();

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
  recognizers.clear();
  models.clear();

  worker_start = chrono::steady_clock::now();
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

  if (frames <= 0 || recognizers.empty())
    return;

  static int silent_streak = 0;

  if (isSilence(buffer.data(), static_cast<int>(frames))) {
    memset(buffer.data(), 0, frames * sizeof(short));

    if (++silent_streak < 10)
      return;
    silent_streak = 0;
  } else {
    silent_streak = 0;
  }

  const char *data = reinterpret_cast<const char *>(buffer.data());
  int len = frames * sizeof(short);

  bool anyFinal = false;
  vector<string> finalJsons(recognizers.size());

  for (size_t i = 0; i < recognizers.size(); ++i) {
    if (vosk_recognizer_accept_waveform(recognizers[i].get(), data, len)) {
      const char *j = vosk_recognizer_result(recognizers[i].get());
      if (j)
        finalJsons[i] = j;
      anyFinal = true;
    }
  }

  if (!anyFinal)
    return;

  struct Hit {
    size_t idx;
    string text;
    double conf;
    CommandMatch match;
  };
  vector<Hit> hits;

  for (size_t i = 0; i < recognizers.size(); ++i) {
    if (finalJsons[i].empty()) {
      const char *j = vosk_recognizer_final_result(recognizers[i].get());
      if (j)
        finalJsons[i] = j;
    }
    vosk_recognizer_reset(recognizers[i].get());

    if (finalJsons[i].empty())
      continue;

    string t = extractTextFromJson(finalJsons[i]);
    if (t.empty())
      continue;

    Hit h{i, std::move(t), avgConfidenceFromJson(finalJsons[i]), {}};
    h.match = findCommandForText(h.text);
    hits.push_back(std::move(h));
  }

  if (debug) {
    for (const auto &h : hits)
      cout << "[" << h.idx << "] conf=" << h.conf
           << " kw=" << h.match.keyword_words << " cmd=\""
           << h.match.script_name << "\""
           << " text=\"" << h.text << "\"\n";
  }

  sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
    if (a.match.keyword_words != b.match.keyword_words)
      return a.match.keyword_words > b.match.keyword_words;
    return a.conf > b.conf;
  });

  constexpr double MIN_CONF = 0.7;

  for (const auto &h : hits) {
    if (h.match.script_name.empty())
      continue;
    if (h.conf < MIN_CONF)
      continue;

    processText(h.text, h.match.script_name);
    break;
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
  modelPaths.clear();

  const char *home = getenv("HOME");

  if (!home) {
    cerr << "HOME environment variable is not set\n";
    return;
  }

  for (const auto &lang : languages) {
    fs::path defaultModelPath =
        fs::path(home) / ".local/share/voice-assistant/models" / lang;
    fs::path localModelPath = fs::path("./models") / lang;
    fs::path chosen;

    if (forceDefaultPaths || forceDefaultModelPath) {
      chosen = defaultModelPath;
    } else if (fs::is_directory(localModelPath)) {
      chosen = localModelPath;
    } else if (fs::is_directory(defaultModelPath)) {
      chosen = defaultModelPath;
    }

    if (chosen.empty() || !fs::is_directory(chosen)) {
      cerr << "Model for language '" << lang << "' not found\n";
      continue;
    }

    modelPaths.push_back(chosen);
    cout << "Model (" << lang << "): " << chosen << '\n';
  }

  if (modelPaths.empty())
    cerr << "No models resolved\n";
}

  
                                 
  
