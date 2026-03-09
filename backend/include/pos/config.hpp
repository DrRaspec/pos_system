#pragma once

#include <cstdint>
#include <string>

namespace pos {

struct AppConfig {
  std::string db_host;
  std::uint16_t db_port;
  std::string db_name;
  std::string db_user;
  std::string db_password;
  std::string db_sslmode;

  std::string bind_address;
  std::uint16_t listen_port;

  std::string cors_origin;
  bool cookie_secure;

  std::int32_t session_ttl_minutes;
  std::int32_t pbkdf2_iterations;
  std::int32_t max_login_attempts;
  std::int32_t login_window_seconds;

  std::string token_pepper;

  std::string bootstrap_admin_username;
  std::string bootstrap_admin_password;

  [[nodiscard]] std::string connectionString() const;
};

AppConfig loadConfigFromEnv();

}  // namespace pos