#include "pos/http_utils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pos {
namespace {

std::string trim(std::string value) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

}  // namespace

void setSecurityHeaders(httplib::Response& res) {
  res.set_header("X-Content-Type-Options", "nosniff");
  res.set_header("X-Frame-Options", "DENY");
  res.set_header("Referrer-Policy", "no-referrer");
  res.set_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
  res.set_header("Cache-Control", "no-store");
}

void applyCors(const httplib::Request& req, httplib::Response& res, const std::string& allowed_origin) {
  if (!req.has_header("Origin")) {
    return;
  }

  const auto origin = req.get_header_value("Origin");
  if (origin == allowed_origin) {
    res.set_header("Access-Control-Allow-Origin", origin);
    res.set_header("Access-Control-Allow-Credentials", "true");
    res.set_header("Vary", "Origin");
  }
}

bool handleCorsPreflight(const httplib::Request& req,
                         httplib::Response& res,
                         const std::string& allowed_origin) {
  if (req.method != "OPTIONS") {
    return false;
  }

  applyCors(req, res, allowed_origin);
  res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
  res.set_header("Access-Control-Max-Age", "600");
  res.status = 204;
  return true;
}

void jsonResponse(httplib::Response& res, int status_code, const nlohmann::json& payload) {
  res.status = status_code;
  res.set_content(payload.dump(), "application/json");
}

std::optional<nlohmann::json> parseJsonBody(const httplib::Request& req,
                                            httplib::Response& res,
                                            const std::string& error_prefix) {
  if (req.body.empty()) {
    jsonResponse(res, 400, {{"error", error_prefix + ": request body is empty."}});
    return std::nullopt;
  }

  try {
    return nlohmann::json::parse(req.body);
  } catch (const std::exception&) {
    jsonResponse(res, 400, {{"error", error_prefix + ": malformed body."}});
    return std::nullopt;
  }
}

std::string getClientIp(const httplib::Request& req) {
  if (req.has_header("X-Forwarded-For")) {
    auto value = req.get_header_value("X-Forwarded-For");
    const auto comma_pos = value.find(',');
    if (comma_pos != std::string::npos) {
      value = value.substr(0, comma_pos);
    }
    return trim(value);
  }

  return req.remote_addr;
}

std::optional<std::string> extractSessionToken(const httplib::Request& req) {
  if (req.has_header("Authorization")) {
    const std::string auth = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    if (auth.rfind(prefix, 0) == 0 && auth.size() > prefix.size()) {
      return auth.substr(prefix.size());
    }
  }

  if (!req.has_header("Cookie")) {
    return std::nullopt;
  }

  std::stringstream ss(req.get_header_value("Cookie"));
  std::string cookie_part;
  while (std::getline(ss, cookie_part, ';')) {
    const auto eq_pos = cookie_part.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    const auto key = trim(cookie_part.substr(0, eq_pos));
    const auto value = trim(cookie_part.substr(eq_pos + 1));
    if (key == "pos_session" && !value.empty()) {
      return value;
    }
  }

  return std::nullopt;
}

std::string buildSessionCookie(const std::string& token, int max_age_seconds, bool secure_cookie) {
  std::ostringstream oss;
  oss << "pos_session=" << token << "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" << max_age_seconds;
  if (secure_cookie) {
    oss << "; Secure";
  }
  return oss.str();
}

std::string buildExpiredSessionCookie() {
  return "pos_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

}  // namespace pos