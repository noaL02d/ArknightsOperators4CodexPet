#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class SessionStatus { Idle, Running, Done, Aborted, Failed };

struct SessionMessage {
  std::string threadId;
  std::string title;
  SessionStatus status = SessionStatus::Idle;
  int64_t startedAt = 0;
  int64_t completedAt = 0;
  int64_t updatedAt = 0;
  bool unread = false;

  int64_t sortTime() const {
    return (status == SessionStatus::Done || status == SessionStatus::Failed) ? completedAt
                                                                             : startedAt;
  }
};

class SessionMonitor {
 public:
  void tick(int64_t nowMs);
  void refreshReceipts(int64_t nowMs);

  const std::vector<SessionMessage>& visible() const { return visible_; }
  bool hasRunning() const { return hasRunning_; }
  bool hasUnread() const { return hasUnread_; }
  uint64_t revision() const { return revision_; }

 private:
  struct Runtime {
    std::string path;
    uint64_t offset = 0;
    std::string remainder;
    SessionStatus status = SessionStatus::Idle;
    int64_t startedAt = 0;
    int64_t completedAt = 0;
    int64_t updatedAt = 0;
  };

  void refreshIndex(int64_t nowMs);
  void scanRollouts(int64_t nowMs);
  void tailRuntime(const std::string& id, Runtime& runtime);
  void applyLine(Runtime& runtime, const std::string& line);
  void rebuild();

  std::vector<std::pair<std::string, std::string>> recent_;  // id, title
  std::map<std::string, std::string> rolloutById_;
  std::set<std::string> subagentIds_;
  std::map<std::string, Runtime> runtimes_;
  std::map<std::string, bool> unread_;
  std::vector<SessionMessage> visible_;
  bool hasRunning_ = false;
  bool hasUnread_ = false;
  uint64_t revision_ = 0;
  int64_t indexAt_ = 0;
  int64_t scanAt_ = 0;
  int64_t receiptsAt_ = 0;
  bool receiptsLoaded_ = false;
};

std::string codexHomePath();
int64_t nowUnixMs();
