#include "pos/config.hpp"
#include "pos/database.hpp"
#include "pos/http_utils.hpp"
#include "pos/rate_limiter.hpp"
#include "pos/security.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using pos::AuthUser;
using pos::ProductRecord;
using pos::SaleItemRequest;
using pos::SaleRecord;
using pos::SaleCreateInput;
using pos::SalePaymentInput;
using pos::CategoryRecord;
using pos::CustomerRecord;
using pos::DiscountRecord;
using pos::ShiftRecord;
using pos::RefundItemInput;

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
  if (!req.has_header("User-Agent")) return "";
  std::string value = req.get_header_value("User-Agent");
  if (value.size() > 255) value.resize(255);
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
      {"category_id", product.category_id},
      {"barcode", product.barcode},
      {"tax_rate_percent", product.tax_rate_percent},
      {"is_active", product.is_active},
  };
}

nlohmann::json toCategoryJson(const CategoryRecord& c) {
  return {{"id", c.id}, {"name", c.name}, {"color", c.color}, {"sort_order", c.sort_order}, {"is_active", c.is_active}};
}

nlohmann::json toCustomerJson(const CustomerRecord& c) {
  return {{"id", c.id}, {"name", c.name}, {"phone", c.phone}, {"email", c.email}, {"loyalty_points", c.loyalty_points}, {"is_active", c.is_active}};
}

nlohmann::json toDiscountJson(const DiscountRecord& d) {
  return {{"id", d.id}, {"name", d.name}, {"type", d.type}, {"value", d.value}, {"promo_code", d.promo_code}, {"min_order_cents", d.min_order_cents}, {"max_uses", d.max_uses}, {"used_count", d.used_count}, {"is_active", d.is_active}};
}

nlohmann::json toShiftJson(const ShiftRecord& s) {
  return {{"id", s.id}, {"opened_by", s.opened_by}, {"closed_by", s.closed_by}, {"opening_cash_cents", s.opening_cash_cents}, {"closing_cash_cents", s.closing_cash_cents}, {"expected_cash_cents", s.expected_cash_cents}, {"notes", s.notes}, {"opened_at", s.opened_at}, {"closed_at", s.closed_at}};
}

nlohmann::json toSaleJson(const SaleRecord& sale) {
  return {
      {"id", sale.id},
      {"receipt_number", sale.receipt_number},
      {"subtotal_cents", sale.subtotal_cents},
      {"tax_cents", sale.tax_cents},
      {"discount_cents", sale.discount_cents},
      {"total_cents", sale.total_cents},
      {"payment_method", sale.payment_method},
      {"created_at", sale.created_at},
      {"sold_by", sale.sold_by},
      {"customer_id", sale.customer_id},
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

    // ============================================================
    // Health
    // ============================================================
    server.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
      pos::jsonResponse(res, 200, {{"status", "ok"}});
    });

    // ============================================================
    // Auth
    // ============================================================
    server.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
      const std::string client_ip = pos::getClientIp(req);
      if (!login_limiter.allow(client_ip)) {
        pos::jsonResponse(res, 429, {{"error", "Too many login attempts. Try again later."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid login payload");
      if (!payload.has_value()) return;

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

      db.createSession(user->id, token_hash, cfg.session_ttl_minutes, client_ip, sanitizeUserAgent(req));

      const int max_age = cfg.session_ttl_minutes * 60;
      res.set_header("Set-Cookie", pos::buildSessionCookie(session_token, max_age, cfg.cookie_secure));

      pos::jsonResponse(res, 200,
                        {{"user", {{"id", user->id}, {"username", user->username}, {"role", user->role}}},
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
      if (!auth_user.has_value()) return;
      pos::jsonResponse(res, 200,
                        {{"user", {{"id", auth_user->user_id}, {"username", auth_user->username}, {"role", auth_user->role}}}});
    });

    // ============================================================
    // Categories
    // ============================================================
    server.Get("/api/categories", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto cats = db.listCategories();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& c : cats) payload.push_back(toCategoryJson(c));
      pos::jsonResponse(res, 200, {{"categories", payload}});
    });

    server.Post("/api/categories", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto payload = pos::parseJsonBody(req, res, "Invalid category payload");
      if (!payload.has_value()) return;
      try {
        const std::string name = (*payload)["name"].get<std::string>();
        const std::string color = payload->value("color", "#6366f1");
        const int sort_order = payload->value("sort_order", 0);
        if (name.empty() || name.size() > 60) throw std::runtime_error("name must be 1-60 characters.");
        const auto cat = db.createCategory(name, color, sort_order, auth_user->user_id);
        pos::jsonResponse(res, 201, {{"category", toCategoryJson(cat)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      } catch (const std::exception&) {
        pos::jsonResponse(res, 409, {{"error", "Could not create category."}});
      }
    });

    server.Patch(R"(/api/categories/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto payload = pos::parseJsonBody(req, res, "Invalid category payload");
      if (!payload.has_value()) return;
      try {
        const int id = std::stoi(std::string(req.matches[1]));
        const std::string name = (*payload)["name"].get<std::string>();
        const std::string color = payload->value("color", "#6366f1");
        const int sort_order = payload->value("sort_order", 0);
        if (!db.updateCategory(id, name, color, sort_order, auth_user->user_id)) {
          pos::jsonResponse(res, 404, {{"error", "Category not found."}});
          return;
        }
        pos::jsonResponse(res, 200, {{"message", "Category updated."}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Delete(R"(/api/categories/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const int id = std::stoi(std::string(req.matches[1]));
      if (!db.deleteCategory(id, auth_user->user_id)) {
        pos::jsonResponse(res, 404, {{"error", "Category not found."}});
        return;
      }
      pos::jsonResponse(res, 200, {{"message", "Category deleted."}});
    });

    // ============================================================
    // Products
    // ============================================================
    server.Get("/api/products", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto products = db.listActiveProducts();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& product : products) payload.push_back(toProductJson(product));
      pos::jsonResponse(res, 200, {{"products", payload}});
    });

    server.Get("/api/products/search", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const std::string q = req.get_param_value("q");
      if (q.empty()) {
        pos::jsonResponse(res, 400, {{"error", "q parameter is required."}});
        return;
      }
      const auto products = db.searchProducts(q);
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& p : products) payload.push_back(toProductJson(p));
      pos::jsonResponse(res, 200, {{"products", payload}});
    });

    server.Get(R"(/api/products/barcode/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const std::string barcode = std::string(req.matches[1]);
      const auto product = db.findProductByBarcode(barcode);
      if (!product.has_value()) {
        pos::jsonResponse(res, 404, {{"error", "Product not found."}});
        return;
      }
      pos::jsonResponse(res, 200, {{"product", toProductJson(*product)}});
    });

    server.Post("/api/products", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid product payload");
      if (!payload.has_value()) return;

      if (!payload->contains("sku") || !(*payload)["sku"].is_string() ||
          !payload->contains("name") || !(*payload)["name"].is_string() ||
          !payload->contains("price_cents") || !payload->contains("stock_quantity")) {
        pos::jsonResponse(res, 400, {{"error", "sku, name, price_cents and stock_quantity are required."}});
        return;
      }

      try {
        const std::string sku = (*payload)["sku"].get<std::string>();
        const std::string name = (*payload)["name"].get<std::string>();
        const int price_cents = parseIntStrict((*payload)["price_cents"], "price_cents");
        const int stock_quantity = parseIntStrict((*payload)["stock_quantity"], "stock_quantity");
        const int category_id = payload->value("category_id", 0);
        const std::string barcode = payload->value("barcode", "");
        const double tax_rate_percent = payload->value("tax_rate_percent", 0.0);

        if (!isValidSku(sku)) throw std::runtime_error("sku must match pattern [A-Z0-9_-]{3,32}.");
        if (!isValidProductName(name)) throw std::runtime_error("name must be 1..120 characters.");
        if (price_cents <= 0 || price_cents > 100000000) throw std::runtime_error("price_cents must be between 1 and 100000000.");
        if (stock_quantity < 0 || stock_quantity > 1000000) throw std::runtime_error("stock_quantity must be between 0 and 1000000.");
        if (tax_rate_percent < 0 || tax_rate_percent > 100) throw std::runtime_error("tax_rate_percent must be between 0 and 100.");

        const auto product = db.createProduct(sku, name, price_cents, stock_quantity, category_id, barcode, tax_rate_percent, auth_user->user_id);
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
                   if (!auth_user.has_value()) return;
                   if (!hasRole(*auth_user, {"admin"})) {
                     pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
                     return;
                   }
                   const auto payload = pos::parseJsonBody(req, res, "Invalid stock payload");
                   if (!payload.has_value()) return;
                   try {
                     const int product_id = std::stoi(std::string(req.matches[1]));
                     const int stock_quantity = parseIntStrict((*payload)["stock_quantity"], "stock_quantity");
                     if (stock_quantity < 0 || stock_quantity > 1000000) throw std::runtime_error("stock_quantity must be between 0 and 1000000.");
                     if (!db.updateProductStock(product_id, stock_quantity, auth_user->user_id)) {
                       pos::jsonResponse(res, 404, {{"error", "Product not found."}});
                       return;
                     }
                     pos::jsonResponse(res, 200, {{"message", "Stock updated."}});
                   } catch (const std::runtime_error& ex) {
                     pos::jsonResponse(res, 400, {{"error", ex.what()}});
                   }
                 });

    server.Patch(R"(/api/products/(\d+))",
                 [&](const httplib::Request& req, httplib::Response& res) {
                   const auto auth_user = requireAuth(req, res);
                   if (!auth_user.has_value()) return;
                   if (!hasRole(*auth_user, {"admin"})) {
                     pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
                     return;
                   }
                   const auto payload = pos::parseJsonBody(req, res, "Invalid product payload");
                   if (!payload.has_value()) return;
                   try {
                     const int product_id = std::stoi(std::string(req.matches[1]));
                     if (!payload->contains("name") || !payload->contains("price_cents") || !payload->contains("stock_quantity")) {
                       pos::jsonResponse(res, 400, {{"error", "name, price_cents, and stock_quantity are required."}});
                       return;
                     }
                     const std::string name = (*payload)["name"].get<std::string>();
                     if (name.empty() || name.size() > 120) throw std::runtime_error("name must be between 1 and 120 characters.");
                     const int price_cents = parseIntStrict((*payload)["price_cents"], "price_cents");
                     if (price_cents < 1 || price_cents > 100000000) throw std::runtime_error("price_cents must be between 1 and 100000000.");
                     const int stock_quantity = parseIntStrict((*payload)["stock_quantity"], "stock_quantity");
                     if (stock_quantity < 0 || stock_quantity > 1000000) throw std::runtime_error("stock_quantity must be between 0 and 1000000.");
                     const int category_id = payload->value("category_id", 0);
                     const std::string barcode = payload->value("barcode", "");
                     const double tax_rate_percent = payload->value("tax_rate_percent", 0.0);
                     if (!db.updateProduct(product_id, name, price_cents, stock_quantity, category_id, barcode, tax_rate_percent, auth_user->user_id)) {
                       pos::jsonResponse(res, 404, {{"error", "Product not found."}});
                       return;
                     }
                     pos::jsonResponse(res, 200, {{"message", "Product updated."}});
                   } catch (const std::runtime_error& ex) {
                     pos::jsonResponse(res, 400, {{"error", ex.what()}});
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
        std::string ext = ".jpg";
        if (content_type == "image/png") ext = ".png";
        else if (content_type == "image/webp") ext = ".webp";
        const std::string filename = "product_" + std::to_string(product_id) + ext;
        const std::string upload_dir = "/app/uploads/";
        const std::string filepath = upload_dir + filename;
        std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
          pos::jsonResponse(res, 500, {{"error", "Failed to save image."}});
          return;
        }
        ofs.write(file.content.data(), static_cast<std::streamsize>(file.content.size()));
        ofs.close();
        const std::string image_url = "/uploads/" + filename;
        if (!db.updateProductImage(product_id, image_url, auth_user->user_id)) {
          pos::jsonResponse(res, 404, {{"error", "Product not found."}});
          return;
        }
        pos::jsonResponse(res, 200, {{"image_url", image_url}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    // ============================================================
    // Customers
    // ============================================================
    server.Get("/api/customers", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto customers = db.listCustomers();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& c : customers) payload.push_back(toCustomerJson(c));
      pos::jsonResponse(res, 200, {{"customers", payload}});
    });

    server.Post("/api/customers", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto payload = pos::parseJsonBody(req, res, "Invalid customer payload");
      if (!payload.has_value()) return;
      try {
        const std::string name = (*payload)["name"].get<std::string>();
        const std::string phone = payload->value("phone", "");
        const std::string email = payload->value("email", "");
        if (name.empty() || name.size() > 120) throw std::runtime_error("name must be 1-120 characters.");
        const auto customer = db.createCustomer(name, phone, email, auth_user->user_id);
        pos::jsonResponse(res, 201, {{"customer", toCustomerJson(customer)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      } catch (const std::exception&) {
        pos::jsonResponse(res, 409, {{"error", "Could not create customer (phone may already exist)."}});
      }
    });

    server.Patch(R"(/api/customers/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto payload = pos::parseJsonBody(req, res, "Invalid customer payload");
      if (!payload.has_value()) return;
      try {
        const int id = std::stoi(std::string(req.matches[1]));
        const std::string name = (*payload)["name"].get<std::string>();
        const std::string phone = payload->value("phone", "");
        const std::string email = payload->value("email", "");
        if (!db.updateCustomer(id, name, phone, email, auth_user->user_id)) {
          pos::jsonResponse(res, 404, {{"error", "Customer not found."}});
          return;
        }
        pos::jsonResponse(res, 200, {{"message", "Customer updated."}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Get("/api/customers/search", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const std::string phone = req.get_param_value("phone");
      if (phone.empty()) {
        pos::jsonResponse(res, 400, {{"error", "phone parameter is required."}});
        return;
      }
      const auto customer = db.findCustomerByPhone(phone);
      if (!customer.has_value()) {
        pos::jsonResponse(res, 404, {{"error", "Customer not found."}});
        return;
      }
      pos::jsonResponse(res, 200, {{"customer", toCustomerJson(*customer)}});
    });

    // ============================================================
    // Discounts
    // ============================================================
    server.Get("/api/discounts", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto discounts = db.listDiscounts();
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& d : discounts) payload.push_back(toDiscountJson(d));
      pos::jsonResponse(res, 200, {{"discounts", payload}});
    });

    server.Post("/api/discounts", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto payload = pos::parseJsonBody(req, res, "Invalid discount payload");
      if (!payload.has_value()) return;
      try {
        const std::string name = (*payload)["name"].get<std::string>();
        const std::string type = (*payload)["type"].get<std::string>();
        const int value = parseIntStrict((*payload)["value"], "value");
        const std::string promo_code = payload->value("promo_code", "");
        const int min_order_cents = payload->value("min_order_cents", 0);
        const int max_uses = payload->value("max_uses", 0);
        if (type != "percent" && type != "fixed") throw std::runtime_error("type must be 'percent' or 'fixed'.");
        if (type == "percent" && (value < 1 || value > 100)) throw std::runtime_error("percent value must be 1-100.");
        const auto discount = db.createDiscount(name, type, value, promo_code, min_order_cents, max_uses, auth_user->user_id);
        pos::jsonResponse(res, 201, {{"discount", toDiscountJson(discount)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      } catch (const std::exception&) {
        pos::jsonResponse(res, 409, {{"error", "Could not create discount."}});
      }
    });

    server.Post("/api/discounts/validate", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto payload = pos::parseJsonBody(req, res, "Invalid payload");
      if (!payload.has_value()) return;
      const std::string code = (*payload)["promo_code"].get<std::string>();
      const auto discount = db.validatePromoCode(code);
      if (!discount.has_value()) {
        pos::jsonResponse(res, 404, {{"error", "Invalid or expired promo code."}});
        return;
      }
      pos::jsonResponse(res, 200, {{"discount", toDiscountJson(*discount)}});
    });

    server.Delete(R"(/api/discounts/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const int id = std::stoi(std::string(req.matches[1]));
      if (!db.deactivateDiscount(id, auth_user->user_id)) {
        pos::jsonResponse(res, 404, {{"error", "Discount not found."}});
        return;
      }
      pos::jsonResponse(res, 200, {{"message", "Discount deactivated."}});
    });

    // ============================================================
    // Shifts
    // ============================================================
    server.Post("/api/shifts/open", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto payload = pos::parseJsonBody(req, res, "Invalid shift payload");
      if (!payload.has_value()) return;
      try {
        const int opening_cash_cents = payload->value("opening_cash_cents", 0);
        // Check if user already has an active shift
        const auto existing = db.getActiveShift(auth_user->user_id);
        if (existing.has_value()) {
          pos::jsonResponse(res, 409, {{"error", "You already have an active shift."}, {"shift", toShiftJson(*existing)}});
          return;
        }
        const auto shift = db.openShift(auth_user->user_id, opening_cash_cents);
        pos::jsonResponse(res, 201, {{"shift", toShiftJson(shift)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Post("/api/shifts/close", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto payload = pos::parseJsonBody(req, res, "Invalid shift payload");
      if (!payload.has_value()) return;
      try {
        const int shift_id = parseIntStrict((*payload)["shift_id"], "shift_id");
        const int closing_cash_cents = parseIntStrict((*payload)["closing_cash_cents"], "closing_cash_cents");
        const std::string notes = payload->value("notes", "");
        if (!db.closeShift(shift_id, auth_user->user_id, closing_cash_cents, notes)) {
          pos::jsonResponse(res, 404, {{"error", "Shift not found or already closed."}});
          return;
        }
        const auto closed = db.getShiftById(shift_id);
        pos::jsonResponse(res, 200, {{"message", "Shift closed."}, {"shift", closed.has_value() ? toShiftJson(*closed) : nlohmann::json{}}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Get("/api/shifts/active", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const auto shift = db.getActiveShift(auth_user->user_id);
      if (!shift.has_value()) {
        pos::jsonResponse(res, 200, {{"shift", nullptr}});
        return;
      }
      pos::jsonResponse(res, 200, {{"shift", toShiftJson(*shift)}});
    });

    // ============================================================
    // Sales (V2)
    // ============================================================
    server.Post("/api/sales", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin", "cashier"})) {
        pos::jsonResponse(res, 403, {{"error", "Cashier or admin role required."}});
        return;
      }

      const auto payload = pos::parseJsonBody(req, res, "Invalid sale payload");
      if (!payload.has_value()) return;

      if (!payload->contains("payment_method") || !(*payload)["payment_method"].is_string() ||
          !payload->contains("items") || !(*payload)["items"].is_array()) {
        pos::jsonResponse(res, 400, {{"error", "payment_method and items are required."}});
        return;
      }

      try {
        const std::string payment_method = (*payload)["payment_method"].get<std::string>();
        if (payment_method != "cash" && payment_method != "card" && payment_method != "transfer" && payment_method != "split") {
          throw std::runtime_error("payment_method must be cash, card, transfer, or split.");
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
          if (product_id <= 0) throw std::runtime_error("product_id must be positive.");
          if (quantity <= 0 || quantity > 1000) throw std::runtime_error("quantity must be between 1 and 1000.");
          items.push_back(SaleItemRequest{product_id, quantity});
        }

        const int customer_id = payload->value("customer_id", 0);
        const int discount_id = payload->value("discount_id", 0);
        const int shift_id = payload->value("shift_id", 0);

        std::vector<SalePaymentInput> payments;
        if (payload->contains("payments") && (*payload)["payments"].is_array()) {
          for (const auto& p : (*payload)["payments"]) {
            const std::string m = p["method"].get<std::string>();
            const int a = parseIntStrict(p["amount_cents"], "amount_cents");
            payments.push_back(SalePaymentInput{m, a});
          }
        }

        SaleCreateInput input{auth_user->user_id, payment_method, items, customer_id, discount_id, shift_id, payments};
        const auto sale = db.createSaleV2(input);

        pos::jsonResponse(res, 201, {{"sale", toSaleJson(sale)}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Get("/api/sales", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      int limit = 20;
      if (req.has_param("limit")) {
        try { limit = std::stoi(req.get_param_value("limit")); } catch (const std::exception&) {
          pos::jsonResponse(res, 400, {{"error", "limit must be an integer."}});
          return;
        }
      }
      limit = std::clamp(limit, 1, 100);
      const auto sales = db.listRecentSales(limit);
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& sale : sales) payload.push_back(toSaleJson(sale));
      pos::jsonResponse(res, 200, {{"sales", payload}});
    });

    server.Get(R"(/api/sales/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      const int sale_id = std::stoi(std::string(req.matches[1]));
      const auto detail = db.getSaleDetail(sale_id);
      if (!detail.has_value()) {
        pos::jsonResponse(res, 404, {{"error", "Sale not found."}});
        return;
      }
      nlohmann::json items = nlohmann::json::array();
      for (const auto& it : detail->items) {
        items.push_back({{"product_id", it.product_id}, {"product_name", it.product_name},
                         {"quantity", it.quantity}, {"unit_price_cents", it.unit_price_cents},
                         {"tax_cents", it.tax_cents}, {"discount_cents", it.discount_cents},
                         {"line_total_cents", it.line_total_cents}});
      }
      nlohmann::json payments = nlohmann::json::array();
      for (const auto& p : detail->payments) {
        payments.push_back({{"method", p.method}, {"amount_cents", p.amount_cents}});
      }
      pos::jsonResponse(res, 200, {{"sale", toSaleJson(detail->sale)}, {"items", items}, {"payments", payments}});
    });

    // ============================================================
    // Refunds
    // ============================================================
    server.Post("/api/refunds", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const auto payload = pos::parseJsonBody(req, res, "Invalid refund payload");
      if (!payload.has_value()) return;
      try {
        const int sale_id = parseIntStrict((*payload)["sale_id"], "sale_id");
        const std::string reason = payload->value("reason", "");
        if (!payload->contains("items") || !(*payload)["items"].is_array()) {
          throw std::runtime_error("items array is required.");
        }
        std::vector<RefundItemInput> items;
        for (const auto& it : (*payload)["items"]) {
          items.push_back(RefundItemInput{parseIntStrict(it["product_id"], "product_id"), parseIntStrict(it["quantity"], "quantity")});
        }
        const auto refund = db.createRefund(sale_id, auth_user->user_id, items, reason);
        pos::jsonResponse(res, 201, {{"refund", {{"id", refund.id}, {"sale_id", refund.sale_id}, {"receipt_number", refund.receipt_number}, {"total_cents", refund.total_cents}, {"reason", refund.reason}, {"created_at", refund.created_at}}}});
      } catch (const std::runtime_error& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    server.Get("/api/refunds", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      int limit = 20;
      if (req.has_param("limit")) {
        try { limit = std::stoi(req.get_param_value("limit")); } catch (...) { limit = 20; }
      }
      limit = std::clamp(limit, 1, 100);
      const auto refunds = db.listRefunds(limit);
      nlohmann::json payload = nlohmann::json::array();
      for (const auto& r : refunds) {
        payload.push_back({{"id", r.id}, {"sale_id", r.sale_id}, {"receipt_number", r.receipt_number}, {"total_cents", r.total_cents}, {"reason", r.reason}, {"created_at", r.created_at}});
      }
      pos::jsonResponse(res, 200, {{"refunds", payload}});
    });

    // ============================================================
    // Reports
    // ============================================================
    server.Get("/api/reports/sales", [&](const httplib::Request& req, httplib::Response& res) {
      const auto auth_user = requireAuth(req, res);
      if (!auth_user.has_value()) return;
      if (!hasRole(*auth_user, {"admin"})) {
        pos::jsonResponse(res, 403, {{"error", "Admin role required."}});
        return;
      }
      const std::string start_date = req.get_param_value("start");
      const std::string end_date = req.get_param_value("end");
      if (start_date.empty() || end_date.empty()) {
        pos::jsonResponse(res, 400, {{"error", "start and end date parameters are required (YYYY-MM-DD)."}});
        return;
      }
      try {
        const auto report = db.getSalesReport(start_date, end_date);
        const auto top = db.getTopProducts(10, start_date, end_date);
        nlohmann::json rows = nlohmann::json::array();
        for (const auto& r : report) {
          rows.push_back({{"date", r.date}, {"sale_count", r.sale_count}, {"revenue_cents", r.revenue_cents}, {"cash_cents", r.cash_cents}, {"card_cents", r.card_cents}, {"transfer_cents", r.transfer_cents}, {"refund_cents", r.refund_cents}});
        }
        nlohmann::json top_arr = nlohmann::json::array();
        for (const auto& t : top) {
          top_arr.push_back({{"product_id", t.product_id}, {"product_name", t.product_name}, {"total_quantity", t.total_quantity}, {"total_revenue_cents", t.total_revenue_cents}});
        }
        pos::jsonResponse(res, 200, {{"report", rows}, {"top_products", top_arr}});
      } catch (const std::exception& ex) {
        pos::jsonResponse(res, 400, {{"error", ex.what()}});
      }
    });

    // ============================================================
    // Users
    // ============================================================
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
        payload.push_back({{"id", u.id}, {"username", u.username}, {"role", u.role}, {"is_active", u.is_active}});
      }
      pos::jsonResponse(res, 200, {{"users", payload}});
    });

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
        if (!isValidUsername(username)) throw std::runtime_error("Username must match [A-Za-z0-9_.-]{3,32}.");
        if (password.size() < 8 || password.size() > 256) throw std::runtime_error("Password must be 8-256 characters.");
        if (role != "admin" && role != "cashier") throw std::runtime_error("Role must be admin or cashier.");
        if (db.userExists(username)) throw std::runtime_error("Username already taken.");
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

    // --- Static files: serve uploaded images ---
    server.set_mount_point("/uploads", "/app/uploads");

    // Background thread: clean up expired sessions every 15 minutes
    std::atomic<bool> running{true};
    std::thread cleanup_thread([&db, &running]() {
      while (running.load()) {
        try { db.cleanupExpiredSessions(); } catch (...) { /* log silently */ }
        for (int i = 0; i < 900 && running.load(); ++i) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      }
    });

    std::cout << "POS backend listening on " << cfg.bind_address << ':' << cfg.listen_port << std::endl;

    if (!server.listen(cfg.bind_address, cfg.listen_port)) {
      running.store(false);
      if (cleanup_thread.joinable()) cleanup_thread.join();
      throw std::runtime_error("Failed to start HTTP server.");
    }

    running.store(false);
    if (cleanup_thread.joinable()) cleanup_thread.join();

    return EXIT_SUCCESS;
  } catch (const std::exception& ex) {
    std::cerr << "Fatal startup error: " << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
}
