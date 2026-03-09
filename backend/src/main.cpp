#include "pos/config.hpp"
#include "pos/database.hpp"
#include "pos/http_utils.hpp"
#include "pos/rate_limiter.hpp"
#include "pos/security.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pos::AuthUser;
using pos::ProductRecord;
using pos::SaleItemRequest;
using pos::SaleRecord;

bool hasRole(const AuthUser& user, const std::vector<std::string>& allowed_roles) {
  return std::find(allowed_roles.begin(), allowed_roles.end(), user.role) != allowed_roles.end();
}

bool isValidUsername(const std::string& username) {
  static const std::regex pattern("^[A-Za-z0-9_.-]{3,32}$");
  return std::regex_match(username, pattern);
}

bool isValidSku(const std::string& sku) {
  static const std::regex pattern("^[A-Z0-9_-]{3,32}$");
  return std::regex_match(sku, pattern);
}

bool isValidProductName(const std::string& name) {
  return !name.empty() && name.size() <= 120;
}

std::string sanitizeUserAgent(const httplib::Request& req) {
  if (!req.has_header("User-Agent")) {
    return "";
  }

  std::string value = req.get_header_value("User-Agent");
  if (value.size() > 255) {
    value.resize(255);
  }
  return value;
}

int parseIntStrict(const nlohmann::json& value, const std::string& field_name) {
  if (!value.is_number_integer()) {
    throw std::runtime_error("Field '" + field_name + "' must be an integer.");
  }
  return value.get<int>();
}

nlohmann::json toProductJson(const ProductRecord& product) {
  return {
      {"id", product.id},
      {"sku", product.sku},
      {"name", product.name},
      {"price_cents", product.price_cents},
      {"stock_quantity", product.stock_quantity},
      {"image_url", product.image_url},
      {"is_active", product.is_active},
  };
}

nlohmann::json toSaleJson(const SaleRecord& sale) {
  return {
      {"id", sale.id},
      {"total_cents", sale.total_cents},
      {"payment_method", sale.payment_method},
      {"created_at", sale.created_at},
      {"sold_by", sale.sold_by},
  };
}

}  // namespace

int main() {
  try {
    const pos::AppConfig cfg = pos::loadConfigFromEnv();

    pos::Database db(cfg.connectionString());
    db.verifyConnectivity();

    if (!cfg.bootstrap_admin_username.empty() && !cfg.bootstrap_admin_password.empty()) {
      if (!isValidUsername(cfg.bootstrap_admin_username)) {
        throw std::runtime_error("BOOTSTRAP_ADMIN_USERNAME has invalid format.");
      }
      if (cfg.bootstrap_admin_password.size() < 12) {
        throw std::runtime_error("BOOTSTRAP_ADMIN_PASSWORD must be at least 12 characters.");
      }

      if (!db.userExists(cfg.bootstrap_admin_username)) {
        const std::string salt = pos::generateHexSalt(16);
        const std::string hash =
            pos::pbkdf2HashHex(cfg.bootstrap_admin_password, salt, cfg.pbkdf2_iterations);
        db.createUser(
            cfg.bootstrap_admin_username, hash, salt, cfg.pbkdf2_iterations, "admin", true);
        std::cout << "Bootstrap admin user created for username: " << cfg.bootstrap_admin_username
                  << std::endl;
      }
    }

    pos::RateLimiter login_limiter(cfg.max_login_attempts, cfg.login_window_seconds);

    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(8); };

    server.set_pre_routing_handler([&cfg](const httplib::Request& req, httplib::Response& res) {
      pos::setSecurityHeaders(res);
      pos::applyCors(req, res, cfg.cors_origin);

      if (pos::handleCorsPreflight(req, res, cfg.cors_origin)) {
        return httplib::Server::HandlerResponse::Handled;
      }

      return httplib::Server::HandlerResponse::Unhandled;
    });

    server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
      try {
        std::rethrow_exception(ep);
      } catch (const std::exception&) {
        pos::jsonResponse(res, 500, {{"error", "Unexpected server error."}});
      }
    });

    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
      pos::jsonResponse(res, 200, {{"status", "ok"}});
    });

    const auto requireAuth = [&db, &cfg](const httplib::Request& req,
                                         httplib::Response& res) -> std::optional<AuthUser> {
      const auto token = pos::extractSessionToken(req);
      if (!token.has_value()) {
        pos::jsonResponse(res, 401, {{"error", "Authentication required."}});
        return std::nullopt;
      }

      const std::string token_hash = pos::sha256Hex(*token + cfg.token_pepper);
      const auto auth_user = db.findActiveSessionUser(token_hash);
      if (!auth_user.has_value()) {
        res.set_header("Set-Cookie", pos::buildExpiredSessionCookie());
        pos::jsonResponse(res, 401, {{"error", "Session expired or invalid."}});
        return std::nullopt;
      }

      return auth_user;
    };

    server.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
      const std::string client_ip = pos::getClientIp(req);
      if (!login_limiter.allow(client_ip)) {
        pos::jsonResponse(res, 429, {{"error", "Too many login attempts. Try again later."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid login payload");
      if (!payload.has_value()) {
        return;
      }

      if (!payload->contains("username") || !(*payload)["username"].is_string() ||
          !payload->contains("password") || !(*payload)["password"].is_string()) {
        pos::jsonResponse(res, 400, {{"error", "username and password are required."}});
        return;
      }

      const std::string username = (*payload)["username"].get<std::string>();
      const std::string password = (*payload)["password"].get<std::string>();

      if (!isValidUsername(username) || password.empty() || password.size() > 256) {
        pos::jsonResponse(res, 401, {{"error", "Invalid credentials."}});
        return;
      }

      const auto user = db.findUserByUsername(username);
      if (!user.has_value() || !user->is_active ||
          !pos::verifyPassword(password, user->password_salt, user->password_iterations, user->password_hash)) {
        pos::jsonResponse(res, 401, {{"error", "Invalid credentials."}});
        return;
      }

      const std::string session_token = pos::generateHexToken(32);
      const std::string token_hash = pos::sha256Hex(session_token + cfg.token_pepper);

      db.createSession(
          user->id, token_hash, cfg.session_ttl_minutes, client_ip, sanitizeUserAgent(req));

      const int max_age = cfg.session_ttl_minutes * 60;
      res.set_header("Set-Cookie", pos::buildSessionCookie(session_token, max_age, cfg.cookie_secure));

      pos::jsonResponse(res,
                        200,
                        {{"user",
                          {{"id", user->id},
                           {"username", user->username},
                           {"role", user->role}}},
                         {"expires_in_seconds", max_age}});
    });

    server.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
      const auto token = pos::extractSessionToken(req);
      if (token.has_value()) {
        db.revokeSession(pos::sha256Hex(*token + cfg.token_pepper));
      }

      res.set_header("Set-Cookie", pos::buildExpiredSessionCookie());
      pos::jsonResponse(res, 200, {{"message", "Logged out."}});
    });

    server.Get("/api/me", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) {
        return;
      }

      pos::jsonResponse(res,
                        200,
                        {{"user",
                          {{"id", auth_user->user_id},
                           {"username", auth_user->username},
                           {"role", auth_user->role}}}});
    });

    server.Get("/api/products", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) {
        return;
      }

      (void)auth_user;

      const auto products = db.listActiveProducts();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& product : products) {
        payload.push_back(toProductJson(product));
      }

      pos::jsonResponse(res, 200, {{"products", payload}});
    });

    server.Post("/api/products", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) {
        return;
      }
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid product payload");
      if (!payload.has_value()) {
        return;
      }

      if (!payload->contains("sku") || !(*payload)["sku"].is_string() ||
          !payload->contains("name") || !(*payload)["name"].is_string() ||
          !payload->contains("price_cents") ||
          !payload->contains("stock_quantity")) {
        pos::jsonResponse(res, 400, {{"error", "sku, name, price_cents and stock_quantity are required."}});
        return;
      }

      try {
        const std::string sku = (*payload)["sku"].get<std::string>();
        const std::string name = (*payload)["name"].get<std::string>();
        const int price_cents = parseIntStrict((*payload)["price_cents"], "price_cents");
        const int stock_quantity = parseIntStrict((*payload)["stock_quantity"], "stock_quantity");

        if (!isValidSku(sku)) {
          throw std::runtime_error("sku must match pattern [A-Z0-9_-]{3,32}.");
        }
        if (!isValidProductName(name)) {
          throw std::runtime_error("name must be 1..120 characters.");
        }
        if (price_cents <= 0 || price_cents > 100000000) {
          throw std::runtime_error("price_cents must be between 1 and 100000000.");
        }
        if (stock_quantity < 0 || stock_quantity > 1000000) {
          throw std::runtime_error("stock_quantity must be between 0 and 1000000.");
        }

        const auto product =
            db.createProduct(sku, name, price_cents, stock_quantity, auth_user->user_id);
        pos::jsonResponse(res, 201, {{"product", toProductJson(product)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      } catch (const std::exception&) {
        pos::jsonResponse(res, 409, {{"error", "Could not create product."}});
      }
    });

    server.Patch(R"(/api/products/(\d+)/stock)",
                 [&](const httplib::Request& req, httplib::Response& res) {
                   const auto auth_user = requireAuth(req, res);
                   if (!auth_user.has_value()) {
                     return;
                   }
                   if (!hasRole(*auth_user, {"admin"})) {
                     pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
                     return;
                   }

                   const auto payload = pos::parseJsonBody(req, res, "Invalid stock payload");
                   if (!payload.has_value()) {
                     return;
                   }

                   if (!payload->contains("stock_quantity")) {
                     pos::jsonResponse(res, 400, {{"error", "stock_quantity is required."}});
                     return;
                   }

                   try {
                     if (req.matches.size() < 2) {
                       pos::jsonResponse(res, 400, {{"error", "Invalid product route."}});
                       return;
                     }

                     const int product_id = std::stoi(std::string(req.matches[1]));
                     const int stock_quantity =
                         parseIntStrict((*payload)["stock_quantity"], "stock_quantity");

                     if (stock_quantity < 0 || stock_quantity > 1000000) {
                       throw std::runtime_error("stock_quantity must be between 0 and 1000000.");
                     }

                     const bool updated =
                         db.updateProductStock(product_id, stock_quantity, auth_user->user_id);
                     if (!updated) {
                       pos::jsonResponse(res, 404, {{"error", "Product not found."}});
                       return;
                     }

                     pos::jsonResponse(res, 200, {{"message", "Stock updated."}});
                   } catch (const std::runtime_error& ex) {
                     pos::jsonResponse(res, 400, {{"error", ex.what()}});
                   }
                 });

    server.Post("/api/sales", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) {
        return;
      }
      if (!hasRole(*auth_user, {"admin", "cashier"})) {
        pos::jsonResponse(res, 403, {{"error", "Cashier or admin role required."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid sale payload");
      if (!payload.has_value()) {
        return;
      }

      if (!payload->contains("payment_method") || !(*payload)["payment_method"].is_string() ||
          !payload->contains("items") || !(*payload)["items"].is_array()) {
        pos::jsonResponse(res, 400, {{"error", "payment_method and items are required."}});
        return;
      }

      try {
        const std::string payment_method = (*payload)["payment_method"].get<std::string>();
        if (payment_method != "cash" && payment_method != "card" && payment_method != "transfer") {
          throw std::runtime_error("payment_method must be cash, card, or transfer.");
        }

        std::vector<SaleItemRequest> items;
        const auto& payload_items = (*payload)["items"];
        if (payload_items.empty() || payload_items.size() > 50) {
          throw std::runtime_error("items must contain between 1 and 50 entries.");
        }

        items.reserve(payload_items.size());
        for (const auto& item : payload_items) {
          if (!item.is_object() || !item.contains("product_id") || !item.contains("quantity")) {
            throw std::runtime_error("Each item requires product_id and quantity.");
          }

          const int product_id = parseIntStrict(item["product_id"], "product_id");
          const int quantity = parseIntStrict(item["quantity"], "quantity");

          if (product_id <= 0) {
            throw std::runtime_error("product_id must be positive.");
          }
          if (quantity <= 0 || quantity > 1000) {
            throw std::runtime_error("quantity must be between 1 and 1000.");
          }

          items.push_back(SaleItemRequest{product_id, quantity});
        }

        const auto sale = db.createSale(auth_user->user_id, payment_method, items);
        pos::jsonResponse(res,
                          201,
                          {{"sale",
                            {{"id", sale.id},
                             {"total_cents", sale.total_cents},
                             {"payment_method", sale.payment_method},
                             {"created_at", sale.created_at}}}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Get("/api/sales", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) {
        return;
      }
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }

      int limit = 20;
      if (req.has_param("limit")) {
        try {
          limit = std::stoi(req.get_param_value("limit"));
        } catch (const std::exception&) {
          pos::jsonResponse(res, 400, {{"error", "limit must be an integer."}});
          return;
        }
      }
      limit = std::clamp(limit, 1, 100);

      const auto sales = db.listRecentSales(limit);
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& sale : sales) {
        payload.push_back(toSaleJson(sale));
      }

      pos::jsonResponse(res, 200, {{"sales", payload}});
    });

    // --- GET /api/users (admin) ---
    server.Get("/api/users", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto users = db.listUsers();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& u : users) {
        payload.push_back({
            {"id", u.id},
            {"username", u.username},
            {"role", u.role},
            {"is_active", u.is_active},
        });
      }
      pos::jsonResponse(res, 200, {{"users", payload}});
    });

    // --- POST /api/users (admin) ---
    server.Post("/api/users", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto payload = pos::parseJsonBody(req, res, "Invalid user payload");
      if (!payload.has_value()) return;
      if (!payload->contains("username") || !(*payload)["username"].is_string() ||
          !payload->contains("password") || !(*payload)["password"].is_string() ||
          !payload->contains("role") || !(*payload)["role"].is_string()) {
        pos::jsonResponse(res, 400, {{"error", "username, password, and role are required."}});
        return;
      }
      try {
        const std::string username = (*payload)["username"].get<std::string>();
        const std::string password = (*payload)["password"].get<std::string>();
        const std::string role = (*payload)["role"].get<std::string>();
        if (!isValidUsername(username))
          throw std::runtime_error("Username must match [A-Za-z0-9_.-]{3,32}.");
        if (password.size() < 8 || password.size() > 256)
          throw std::runtime_error("Password must be 8-256 characters.");
        if (role != "admin" && role != "cashier")
          throw std::runtime_error("Role must be admin or cashier.");
        if (db.userExists(username))
          throw std::runtime_error("Username already taken.");
        const std::string salt = pos::generateHexSalt(16);
        const std::string hash = pos::pbkdf2HashHex(password, salt, cfg.pbkdf2_iterations);
        const int user_id = db.createUserByAdmin(username, hash, salt, cfg.pbkdf2_iterations, role, auth_user->user_id);
        pos::jsonResponse(res, 201, {{"user", {{"id", user_id}, {"username", username}, {"role", role}, {"is_active", true}}}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      } catch (const std::exception&) {
        pos::jsonResponse(res, 409, {{"error", "Could not create user."}});
      }
    });

    // --- POST /api/products/:id/image (admin) ---
    server.Post(R"(/api/products/(\d+)/image)",
                [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      if (req.matches.size() < 2) {
        pos::jsonResponse(res, 400, {{"error", "Invalid product route."}});
        return;
      }
      try {
        const int product_id = std::stoi(std::string(req.matches[1]));
        if (!req.has_file("image")) {
          pos::jsonResponse(res, 400, {{"error", "image file is required."}});
          return;
        }
        const auto& file = req.get_file_value("image");
        const std::string& content_type = file.content_type;
        if (content_type != "image/jpeg" && content_type != "image/png" && content_type != "image/webp") {
          pos::jsonResponse(res, 400, {{"error", "Only JPEG, PNG, or WebP images are allowed."}});
          return;
        }
        if (file.content.size() > 2 * 1024 * 1024) {
          pos::jsonResponse(res, 400, {{"error", "Image must be under 2MB."}});
          return;
        }
        // Determine file extension
        std::string ext = ".jpg";
        if (content_type == "image/png") ext = ".png";
        else if (content_type == "image/webp") ext = ".webp";

        const std::string filename = "product_" + std::to_string(product_id) + ext;
        const std::string upload_dir = "/app/uploads/";
        const std::string filepath = upload_dir + filename;

        // Write file
        std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
          pos::jsonResponse(res, 500, {{"error", "Failed to save image."}});
          return;
        }
        ofs.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
        ofs.close();

        const std::string image_url = "/uploads/" + filename;
        const bool updated = db.updateProductImage(product_id, image_url, auth_user->user_id);
        if (!updated) {
          pos::jsonResponse(res, 404, {{"error", "Product not found."}});
          return;
        }
        pos::jsonResponse(res, 200, {{"image_url", image_url}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    // --- Static files: serve uploaded images ---
    server.set_mount_point("/uploads", "/app/uploads");

    std::cout << "POS backend listening on " << cfg.bind_address << ':' << cfg.listen_port << std::endl;

    if (!server.listen(cfg.bind_address, cfg.listen_port)) {
      throw std::runtime_error("Failed to start HTTP server.");
    }

    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal startup error: " << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
}
