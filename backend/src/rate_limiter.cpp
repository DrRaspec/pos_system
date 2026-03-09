#include "pos/rate_limiter.hpp"

namespace pos {

RateLimiter::RateLimiter(int max_attempts, int window_seconds)
    : max_attempts_(max_attempts), window_(std::chrono::seconds(window_seconds)) {}

bool RateLimiter::allow(const std::string& key) {
  const auto now = Clock::now();

  std::lock_guard<std::mutex> lock(mutex_);
  auto& entries = requests_[key];

  while (!entries.empty() && (now - entries.front()) > window_) {
    entries.pop_front();
  }

  if (static_cast<int>(entries.size()) >= max_attempts_) {
    return false;
  }

  entries.push_back(now);

  return true;
}

}  // namespace pos
