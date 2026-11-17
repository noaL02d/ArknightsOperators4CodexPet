#include "session.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>

#include "image.h"
#include "json.h"

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxThreads = 12;
constexpr int64_t kIndexIntervalMs = 1500;
constexpr int64_t kScanIntervalMs = 5000;
constexpr int64_t kReceiptsIntervalMs = 2000;

const std::regex kUuid(
    "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}",
    std::regex::icase);

int64_t parseIso(const std::string& text) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
    return 0;
  }
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  return static_cast<int64_t>(_mkgmtime64(&tm)) * 1000;
}

std::string readSlice(const std::string& path, uint64_t offset) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::string();
  file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return text;
}

}  // namespace

int64_t nowUnixMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string codexHomePath() {
  const char* env = std::getenv("CODEX_HOME");
  if (env && *env) return std::string(env);
  const char* profile = std::getenv("USERPROFILE");
  if (!profile || !*profile) return ".";
  return std::string(profile) + "\\.codex";
}

void SessionMonitor::applyLine(Runtime& runtime, const std::string& line) {
  mini::Json root;
  if (!mini::parseJson(line, root)) return;
  const mini::Json* type = root.find("type");
  if (!type) return;
  int64_t timestamp = 0;
  if (const mini::Json* ts = root.find("timestamp")) timestamp = parseIso(ts->asText());
  if (timestamp > runtime.updatedAt) runtime.updatedAt = timestamp;

  if (type->asText() != "event_msg") return;
  const mini::Json* payload = root.find("payload");
  if (!payload) return;
  const mini::Json* eventType = payload->find("type");
  if (!eventType) return;
  const std::string& name = eventType->text;
  if (name == "task_started") {
    runtime.status = SessionStatus::Running;
    runtime.startedAt = timestamp;
  } else if (name == "task_complete") {
    runtime.status = SessionStatus::Done;
    runtime.completedAt = timestamp;
    if (!runtime.startedAt) runtime.startedAt = timestamp;
  } else if (name == "turn_aborted") {
    runtime.status = SessionStatus::Aborted;
    runtime.completedAt = timestamp;
  } else if (name == "error") {
    runtime.status = SessionStatus::Failed;
    runtime.completedAt = timestamp;
  }
}

void SessionMonitor::tailRuntime(const std::string& id, Runtime& runtime) {
  std::error_code error;
  auto size = fs::file_size(runtime.path, error);
  if (error) return;
  if (static_cast<uint64_t>(size) < runtime.offset) {
    runtime.offset = 0;
    runtime.remainder.clear();
  }
  if (static_cast<uint64_t>(size) == runtime.offset) return;
  std::string chunk = readSlice(runtime.path, runtime.offset);
  runtime.offset = static_cast<uint64_t>(size);
  std::string text = runtime.remainder + chunk;
  size_t start = 0;
  while (true) {
    size_t newline = text.find('\n', start);
    if (newline == std::string::npos) break;
    std::string line = text.substr(start, newline - start);
    start = newline + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) applyLine(runtime, line);
  }
  runtime.remainder = text.substr(start);
  if (runtime.remainder.size() > (1u << 20)) runtime.remainder.clear();
  (void)id;
}

void SessionMonitor::scanRollouts(int64_t nowMs) {
  if (nowMs - scanAt_ < kScanIntervalMs) return;
  scanAt_ = nowMs;
  std::map<std::string, std::string> map;
  std::set<std::string> subs;
  std::string root = codexHomePath() + "\\sessions";
  std::error_code error;
  if (!fs::exists(root, error)) return;
  for (int daysAgo = 0; daysAgo < 3; ++daysAgo) {
    std::time_t day = std::time(nullptr) - static_cast<std::time_t>(daysAgo) * 86400;
    std::tm local{};
    localtime_s(&local, &day);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%04d\\%02d\\%02d", local.tm_year + 1900, local.tm_mon + 1,
             local.tm_mday);
    fs::path dir = fs::path(root) / buffer;
    if (!fs::exists(dir, error)) continue;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
      if (error) break;
      if (!it->is_regular_file()) continue;
      const std::string name = it->path().filename().string();
      if (name.size() < 6 || name.compare(name.size() - 6, 6, ".jsonl") != 0) continue;
      std::vector<std::string> ids;
      for (std::sregex_iterator found(name.begin(), name.end(), kUuid), last; found != last;
           ++found) {
        ids.push_back(found->str());
      }
      if (ids.empty()) continue;
      bool isSub = name.find('_') != std::string::npos;
      for (const std::string& id : ids) map[id] = it->path().string();
      if (isSub) {
        for (size_t i = 1; i < ids.size(); ++i) subs.insert(ids[i]);
      }
    }
  }
  rolloutById_ = std::move(map);
  subagentIds_ = std::move(subs);
}

void SessionMonitor::refreshIndex(int64_t nowMs) {
  if (nowMs - indexAt_ < kIndexIntervalMs) return;
  indexAt_ = nowMs;
  std::string text;
  if (!readFileText(widen(codexHomePath() + "\\session_index.jsonl"), text)) return;
  std::map<std::string, std::pair<std::string, int64_t>> byId;
  size_t start = 0;
  while (start < text.size()) {
    size_t newline = text.find('\n', start);
    if (newline == std::string::npos) newline = text.size();
    std::string line = text.substr(start, newline - start);
    start = newline + 1;
    if (line.empty()) continue;
    mini::Json entry;
    if (!mini::parseJson(line, entry)) continue;
    const mini::Json* id = entry.find("id");
    if (!id || id->type != mini::Json::Type::String) continue;
    const mini::Json* name = entry.find("thread_name");
    const mini::Json* updated = entry.find("updated_at");
    int64_t when = updated ? parseIso(updated->asText()) : 0;
    std::string idText = id->text;
    auto existing = byId.find(idText);
    if (existing == byId.end() || existing->second.second < when) {
      byId[idText] = {name ? name->asText(idText) : idText, when};
    }
  }
  std::vector<std::pair<std::string, std::string>> recent;
  for (const auto& item : byId) {
    if (subagentIds_.count(item.first)) continue;
    recent.emplace_back(item.first, item.second.first);
  }
  std::sort(recent.begin(), recent.end(), [&](const auto& a, const auto& b) {
    return byId[a.first].second > byId[b.first].second;
  });
  if (recent.size() > 64) recent.resize(64);
  recent_ = std::move(recent);
}

void SessionMonitor::tick(int64_t nowMs) {
  scanRollouts(nowMs);
  refreshIndex(nowMs);
  size_t limit = std::min(recent_.size(), kMaxThreads);
  for (size_t i = 0; i < limit; ++i) {
    const std::string& id = recent_[i].first;
    auto existing = runtimes_.find(id);
    if (existing != runtimes_.end()) continue;
    auto path = rolloutById_.find(id);
    if (path == rolloutById_.end()) continue;
    Runtime runtime;
    runtime.path = path->second;
    runtimes_.emplace(id, std::move(runtime));
  }
  for (size_t i = 0; i < limit; ++i) {
    const std::string& id = recent_[i].first;
    auto runtime = runtimes_.find(id);
    if (runtime == runtimes_.end()) continue;
    tailRuntime(id, runtime->second);
  }
  rebuild();
}

void SessionMonitor::refreshReceipts(int64_t nowMs) {
  if (receiptsLoaded_ && nowMs - receiptsAt_ < kReceiptsIntervalMs) return;
  receiptsAt_ = nowMs;
  std::string path = codexHomePath() + "\\state_5.sqlite";
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (db) sqlite3_close(db);
    return;
  }
  sqlite3_busy_timeout(db, 200);
  std::map<std::string, bool> unread;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, "select thread_id, first_unread_turn from thread_read_receipts", -1,
                         &statement, nullptr) == SQLITE_OK) {
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const unsigned char* id = sqlite3_column_text(statement, 0);
      if (!id) continue;
      bool hasUnread = sqlite3_column_type(statement, 1) != SQLITE_NULL;
      unread[reinterpret_cast<const char*>(id)] = hasUnread;
    }
    sqlite3_finalize(statement);
  }
  sqlite3_close(db);
  unread_ = std::move(unread);
  receiptsLoaded_ = true;
}

void SessionMonitor::rebuild() {
  std::vector<SessionMessage> list;
  for (const auto& entry : recent_) {
    auto runtime = runtimes_.find(entry.first);
    if (runtime == runtimes_.end()) continue;
    const Runtime& rt = runtime->second;
    if (rt.status == SessionStatus::Idle || rt.status == SessionStatus::Aborted) continue;
    bool unread = false;
    auto receipt = unread_.find(entry.first);
    if (receipt != unread_.end()) unread = receipt->second;
    SessionMessage message;
    message.threadId = entry.first;
    message.title = entry.second.empty() ? entry.first.substr(0, 8) : entry.second;
    message.status = rt.status;
    message.startedAt = rt.startedAt;
    message.completedAt = rt.completedAt;
    message.updatedAt = rt.updatedAt;
    message.unread = unread;
    bool isRunning = rt.status == SessionStatus::Running;
    bool isUnreadDone = rt.status == SessionStatus::Done && unread;
    if (!isRunning && !isUnreadDone) continue;
    list.push_back(std::move(message));
  }
  std::sort(list.begin(), list.end(),
            [](const SessionMessage& a, const SessionMessage& b) {
              return a.sortTime() > b.sortTime();
            });
  if (list.size() > kMaxThreads) list.resize(kMaxThreads);

  bool changed = list.size() != visible_.size();
  if (!changed) {
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].threadId != visible_[i].threadId || list[i].status != visible_[i].status) {
        changed = true;
        break;
      }
    }
  }
  visible_ = std::move(list);
  hasRunning_ = false;
  hasUnread_ = false;
  for (const SessionMessage& message : visible_) {
    if (message.status == SessionStatus::Running) hasRunning_ = true;
    if (message.status == SessionStatus::Done) hasUnread_ = true;
  }
  if (changed) ++revision_;
}
