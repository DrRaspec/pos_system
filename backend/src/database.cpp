#include "pos/database.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pos {
namespace {

std::unique_ptr<pqxx::connection> openConnection(const std::string& connection_string) {
  auto conn = std::make_unique<pqxx::connection>(connection_string);
  if (!conn->is_open()) {
    throw std::runtime_error("Failed to connect to PostgreSQL.");
  }

  conn->prepare(
      "find_user_by_username",
      "SELECT id, username, password_hash, password_salt, password_iterations, role, is_active "
      "FROM users WHERE username = $1 LIMIT 1");

  conn->prepare(
      "find_session_user",
      "SELECT u.id, u.username, u.role "
      "FROM sessions s "
      "JOIN users u ON u.id = s.user_id "
      "WHERE s.token_hash = $1 "
      "AND s.revoked_at IS NULL "
      "AND s.expires_at > now() "
      "AND u.is_active = TRUE "
      "LIMIT 1");

  conn->prepare("insert_session",
                "INSERT INTO sessions (user_id, token_hash, expires_at, ip_address, user_agent) "
                "VALUES ($1, $2, now() + ($3::text || ' minutes')::interval, $4, $5)");

  conn->prepare("revoke_session",
                "UPDATE sessions SET revoked_at = now() "
                "WHERE token_hash = $1 AND revoked_at IS NULL");

  conn->prepare("list_products",
                "SELECT id, sku, name, price_cents, stock_quantity, image_url, is_active "
                "FROM products WHERE is_active = TRUE ORDER BY name ASC");

  conn->prepare("insert_product",
                "INSERT INTO products (sku, name, price_cents, stock_quantity, is_active) "
                "VALUES ($1, $2, $3, $4, TRUE) "
                "RETURNING id, sku, name, price_cents, stock_quantity, image_url, is_active");

  conn->prepare("update_product_stock",
                "UPDATE products SET stock_quantity = $1, updated_at = now() "
                "WHERE id = $2");

  conn->prepare("fetch_product_for_update",
                "SELECT id, price_cents, stock_quantity, is_active, name "
                "FROM products WHERE id = $1 FOR UPDATE");

  conn->prepare("insert_sale",
                "INSERT INTO sales (sold_by_user_id, total_cents, payment_method) "
                "VALUES ($1, $2, $3) "
                "RETURNING id, total_cents, payment_method, created_at");

  conn->prepare("insert_sale_item",
                "INSERT INTO sale_items (sale_id, product_id, quantity, unit_price_cents, line_total_cents) "
                "VALUES ($1, $2, $3, $4, $5)");

  conn->prepare("decrement_stock",
                "UPDATE products SET stock_quantity = stock_quantity - $1, updated_at = now() "
                "WHERE id = $2");

  conn->prepare("insert_audit",
                "INSERT INTO audit_logs (actor_user_id, action, detail) VALUES ($1, $2, $3::jsonb)");

  conn->prepare("list_recent_sales",
                "SELECT s.id, s.total_cents, s.payment_method, s.created_at, u.username "
                "FROM sales s "
                "JOIN users u ON u.id = s.sold_by_user_id "
                "ORDER BY s.created_at DESC "
                "LIMIT $1");

  conn->prepare("user_exists", "SELECT EXISTS (SELECT 1 FROM users WHERE username = $1)");

  conn->prepare("insert_user",
                "INSERT INTO users (username, password_hash, password_salt, password_iterations, role, is_active) "
                "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id");

  conn->prepare("list_users",
                "SELECT id, username, role, is_active, created_at "
                "FROM users ORDER BY id ASC");

  conn->prepare("update_product_image",
                "UPDATE products SET image_url = $1, updated_at = now() WHERE id = $2");

  return conn;
}

}  // namespace

Database::Database(std::string connection_string) : connection_string_(std::move(connection_string)) {}

void Database::verifyConnectivity() const {
  const auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);
  tx.exec("SELECT 1");
}

std::optional<UserRecord> Database::findUserByUsername(const std::string& username) const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result rows = tx.exec_prepared("find_user_by_username", username);
  if (rows.empty()) {
    return std::nullopt;
  }

  const auto row = rows[0];
  return UserRecord{
      row["id"].as<int>(),
      row["username"].as<std::string>(),
      row["password_hash"].as<std::string>(),
      row["password_salt"].as<std::string>(),
      row["password_iterations"].as<int>(),
      row["role"].as<std::string>(),
      row["is_active"].as<bool>(),
  };
}

std::optional<AuthUser> Database::findActiveSessionUser(const std::string& token_hash) const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result rows = tx.exec_prepared("find_session_user", token_hash);
  if (rows.empty()) {
    return std::nullopt;
  }

  const auto row = rows[0];
  return AuthUser{
      row["id"].as<int>(),
      row["username"].as<std::string>(),
      row["role"].as<std::string>(),
  };
}

void Database::createSession(int user_id,
                             const std::string& token_hash,
                             int session_ttl_minutes,
                             const std::string& client_ip,
                             const std::string& user_agent) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  tx.exec_prepared("insert_session", user_id, token_hash, session_ttl_minutes, client_ip, user_agent);
  tx.commit();
}

void Database::revokeSession(const std::string& token_hash) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);
  tx.exec_prepared("revoke_session", token_hash);
  tx.commit();
}

std::vector<ProductRecord> Database::listActiveProducts() const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result rows = tx.exec_prepared("list_products");
  std::vector<ProductRecord> products;
  products.reserve(rows.size());

  for (const auto& row : rows) {
    products.push_back(ProductRecord{
        row["id"].as<int>(),
        row["sku"].as<std::string>(),
        row["name"].as<std::string>(),
        row["price_cents"].as<int>(),
        row["stock_quantity"].as<int>(),
        row["image_url"].as<std::string>(),
        row["is_active"].as<bool>(),
    });
  }

  return products;
}

ProductRecord Database::createProduct(const std::string& sku,
                                      const std::string& name,
                                      int price_cents,
                                      int stock_quantity,
                                      int actor_user_id) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  const auto row = tx.exec_prepared1("insert_product", sku, name, price_cents, stock_quantity);

  nlohmann::json detail = {
      {"product_id", row["id"].as<int>()},
      {"sku", sku},
      {"name", name},
      {"price_cents", price_cents},
      {"stock_quantity", stock_quantity},
  };

  tx.exec_prepared("insert_audit", actor_user_id, "product.created", detail.dump());
  tx.commit();

  return ProductRecord{
      row["id"].as<int>(),
      row["sku"].as<std::string>(),
      row["name"].as<std::string>(),
      row["price_cents"].as<int>(),
      row["stock_quantity"].as<int>(),
      row["image_url"].as<std::string>(),
      row["is_active"].as<bool>(),
  };
}

bool Database::updateProductStock(int product_id, int stock_quantity, int actor_user_id) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  const pqxx::result update_result = tx.exec_prepared("update_product_stock", stock_quantity, product_id);
  const bool updated = update_result.affected_rows() == 1;

  if (updated) {
    nlohmann::json detail = {
        {"product_id", product_id},
        {"stock_quantity", stock_quantity},
    };
    tx.exec_prepared("insert_audit", actor_user_id, "product.stock_updated", detail.dump());
  }

  tx.commit();
  return updated;
}

SaleRecord Database::createSale(int sold_by_user_id,
                                const std::string& payment_method,
                                const std::vector<SaleItemRequest>& items) const {
  if (items.empty()) {
    throw std::runtime_error("At least one sale item is required.");
  }

  struct ComputedLine {
    int product_id;
    int quantity;
    int unit_price_cents;
    int line_total_cents;
    std::string name;
  };

  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  std::vector<ComputedLine> computed;
  computed.reserve(items.size());

  int total_cents = 0;
  for (const auto& item : items) {
    const pqxx::result product_rows = tx.exec_prepared("fetch_product_for_update", item.product_id);
    if (product_rows.empty()) {
      throw std::runtime_error("Product not found.");
    }

    const auto row = product_rows[0];
    const bool is_active = row["is_active"].as<bool>();
    const int stock_quantity = row["stock_quantity"].as<int>();
    const int unit_price_cents = row["price_cents"].as<int>();
    const std::string name = row["name"].as<std::string>();

    if (!is_active) {
      throw std::runtime_error("Product is inactive.");
    }
    if (item.quantity > stock_quantity) {
      throw std::runtime_error("Insufficient stock for " + name + ".");
    }

    const int line_total = unit_price_cents * item.quantity;
    total_cents += line_total;

    computed.push_back(ComputedLine{item.product_id, item.quantity, unit_price_cents, line_total, name});
  }

  const auto sale_row = tx.exec_prepared1("insert_sale", sold_by_user_id, total_cents, payment_method);
  const int sale_id = sale_row["id"].as<int>();

  nlohmann::json audit_items = nlohmann::json::array();
  for (const auto& line : computed) {
    tx.exec_prepared(
        "insert_sale_item", sale_id, line.product_id, line.quantity, line.unit_price_cents, line.line_total_cents);
    tx.exec_prepared("decrement_stock", line.quantity, line.product_id);

    audit_items.push_back({
        {"product_id", line.product_id},
        {"name", line.name},
        {"quantity", line.quantity},
        {"unit_price_cents", line.unit_price_cents},
        {"line_total_cents", line.line_total_cents},
    });
  }

  nlohmann::json detail = {
      {"sale_id", sale_id},
      {"payment_method", payment_method},
      {"total_cents", total_cents},
      {"items", audit_items},
  };
  tx.exec_prepared("insert_audit", sold_by_user_id, "sale.created", detail.dump());

  tx.commit();

  return SaleRecord{
      sale_id,
      sale_row["total_cents"].as<int>(),
      sale_row["payment_method"].as<std::string>(),
      std::string(sale_row["created_at"].c_str()),
      std::string(),
  };
}

std::vector<SaleRecord> Database::listRecentSales(int limit) const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result rows = tx.exec_prepared("list_recent_sales", limit);
  std::vector<SaleRecord> sales;
  sales.reserve(rows.size());

  for (const auto& row : rows) {
    sales.push_back(SaleRecord{
        row["id"].as<int>(),
        row["total_cents"].as<int>(),
        row["payment_method"].as<std::string>(),
        std::string(row["created_at"].c_str()),
        row["username"].as<std::string>(),
    });
  }

  return sales;
}

bool Database::userExists(const std::string& username) const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);
  const auto row = tx.exec_prepared1("user_exists", username);
  return row[0].as<bool>();
}

void Database::createUser(const std::string& username,
                          const std::string& password_hash,
                          const std::string& password_salt,
                          int password_iterations,
                          const std::string& role,
                          bool is_active) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  const auto row = tx.exec_prepared1(
      "insert_user", username, password_hash, password_salt, password_iterations, role, is_active);
  const int user_id = row["id"].as<int>();

  nlohmann::json detail = {
      {"user_id", user_id},
      {"username", username},
      {"role", role},
      {"is_active", is_active},
      {"source", "bootstrap"},
  };
  tx.exec_prepared("insert_audit", user_id, "user.created", detail.dump());

  tx.commit();
}

std::vector<UserRecord> Database::listUsers() const {
  auto conn = openConnection(connection_string_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result rows = tx.exec_prepared("list_users");
  std::vector<UserRecord> users;
  users.reserve(rows.size());

  for (const auto& row : rows) {
    users.push_back(UserRecord{
        row["id"].as<int>(),
        row["username"].as<std::string>(),
        "",  // password_hash - not exposed
        "",  // password_salt - not exposed
        0,   // password_iterations - not exposed
        row["role"].as<std::string>(),
        row["is_active"].as<bool>(),
    });
  }

  return users;
}

int Database::createUserByAdmin(const std::string& username, const std::string& password_hash,
                                const std::string& password_salt, int password_iterations,
                                const std::string& role, int actor_user_id) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  const auto row = tx.exec_prepared1(
      "insert_user", username, password_hash, password_salt, password_iterations, role, true);
  const int user_id = row["id"].as<int>();

  nlohmann::json detail = {
      {"user_id", user_id},
      {"username", username},
      {"role", role},
      {"source", "admin"},
  };
  tx.exec_prepared("insert_audit", actor_user_id, "user.created", detail.dump());

  tx.commit();
  return user_id;
}

bool Database::updateProductImage(int product_id, const std::string& image_url, int actor_user_id) const {
  auto conn = openConnection(connection_string_);
  pqxx::work tx(*conn);

  const pqxx::result update_result = tx.exec_prepared("update_product_image", image_url, product_id);
  const bool updated = update_result.affected_rows() == 1;

  if (updated) {
    nlohmann::json detail = {
        {"product_id", product_id},
        {"image_url", image_url},
    };
    tx.exec_prepared("insert_audit", actor_user_id, "product.image_updated", detail.dump());
  }

  tx.commit();
  return updated;
}

}  // namespace pos