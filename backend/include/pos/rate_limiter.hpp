#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pos {

class RateLimiter {
 public:
  RateLimiter(int max_attempts, int window_seconds);

  bool allow(const std::string& key);

 private:
  using Clock = std::chrono::steady_clock;

  int max_attempts_;
  std::chrono::seconds window_;

  std::unordered_map<std::string, std::deque<Clock::time_point>> requests_;
  std::mutex mutex_;
};

}  // namespace pos