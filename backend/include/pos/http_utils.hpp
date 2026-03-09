#pragma once

#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace pos {

void setSecurityHeaders(httplib::Response& res);
void applyCors(const httplib::Request& req, httplib::Response& res, const std::string& allowed_origin);
bool handleCorsPreflight(const httplib::Request& req,
                         httplib::Response& res,
                         const std::string& allowed_origin);

void jsonResponse(httplib::Response& res, int status_code, const nlohmann::json& payload);
std::optional<nlohmann::json> parseJsonBody(const httplib::Request& req,
                                            httplib::Response& res,
                                            const std::string& error_prefix = "Invalid JSON");

std::string getClientIp(const httplib::Request& req);
std::optional<std::string> extractSessionToken(const httplib::Request& req);

std::string buildSessionCookie(const std::string& token, int max_age_seconds, bool secure_cookie);
std::string buildExpiredSessionCookie();

}  // namespace pos