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

  // Periodically clean up stale keys to prevent memory growth
  if (++cleanup_counter_ >= 100) {
    cleanup_counter_ = 0;
    for (auto it = requests_.begin(); it != requests_.end(); ) {
      if (it->second.empty()) {
        it = requests_.erase(it);
      } else {
        ++it;
      }
    }
  }

  return true;
}

}  // namespace pos
