#include "pos/config.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace pos {
namespace {

std::string getEnvOrDefault(const char* key, const std::string& default_value) {
  const char* raw = std::getenv(key);
  return raw == nullptr ? default_value : std::string(raw);
}

std::string getRequiredEnv(const char* key) {
  const char* raw = std::getenv(key);
  if (raw == nullptr || std::string(raw).empty()) {
    throw std::runtime_error(std::string("Missing required environment variable: ") + key);
  }
  return std::string(raw);
}

std::int32_t parseInt(const std::string& value, const char* name) {
  try {
    std::size_t idx = 0;
    const int parsed = std::stoi(value, &idx);
    if (idx != value.size()) {
      throw std::runtime_error("trailing characters");
    }
    return parsed;
  } catch (const std::exception&) {
    throw std::runtime_error(std::string("Invalid integer for ") + name + ": " + value);
  }
}

bool parseBool(const std::string& value, const char* name) {
  if (value == "true" || value == "1" || value == "TRUE") {
    return true;
  }
  if (value == "false" || value == "0" || value == "FALSE") {
    return false;
  }
  throw std::runtime_error(std::string("Invalid boolean for ") + name + ": " + value);
}

}  // namespace

std::string AppConfig::connectionString() const {
  std::ostringstream oss;
  oss << "host=" << db_host << ' ';
  oss << "port=" << db_port << ' ';
  oss << "dbname=" << db_name << ' ';
  oss << "user=" << db_user << ' ';
  oss << "password=" << db_password << ' ';
  oss << "sslmode=" << db_sslmode;
  return oss.str();
}

AppConfig loadConfigFromEnv() {
  AppConfig cfg{};

  cfg.db_host = getEnvOrDefault("DB_HOST", "db");
  cfg.db_port = static_cast<std::uint16_t>(parseInt(getEnvOrDefault("DB_PORT", "5432"), "DB_PORT"));
  cfg.db_name = getEnvOrDefault("DB_NAME", "posdb");
  cfg.db_user = getEnvOrDefault("DB_USER", "posapp");
  cfg.db_password = getRequiredEnv("DB_PASSWORD");
  cfg.db_sslmode = getEnvOrDefault("DB_SSLMODE", "disable");

  cfg.bind_address = getEnvOrDefault("SERVER_HOST", "0.0.0.0");
  cfg.listen_port = static_cast<std::uint16_t>(parseInt(getEnvOrDefault("SERVER_PORT", "8080"), "SERVER_PORT"));

  cfg.cors_origin = getEnvOrDefault("CORS_ORIGIN", "http://localhost:3000");
  cfg.cookie_secure = parseBool(getEnvOrDefault("COOKIE_SECURE", "false"), "COOKIE_SECURE");

  cfg.session_ttl_minutes = parseInt(getEnvOrDefault("SESSION_TTL_MINUTES", "480"), "SESSION_TTL_MINUTES");
  cfg.pbkdf2_iterations = parseInt(getEnvOrDefault("PBKDF2_ITERATIONS", "210000"), "PBKDF2_ITERATIONS");
  cfg.max_login_attempts = parseInt(getEnvOrDefault("LOGIN_MAX_ATTEMPTS", "8"), "LOGIN_MAX_ATTEMPTS");
  cfg.login_window_seconds = parseInt(getEnvOrDefault("LOGIN_WINDOW_SECONDS", "300"), "LOGIN_WINDOW_SECONDS");

  cfg.token_pepper = getRequiredEnv("SESSION_TOKEN_PEPPER");

  cfg.bootstrap_admin_username = getEnvOrDefault("BOOTSTRAP_ADMIN_USERNAME", "");
  cfg.bootstrap_admin_password = getEnvOrDefault("BOOTSTRAP_ADMIN_PASSWORD", "");

  return cfg;
}

}  // namespace pos