#include "pos/database.hpp"

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pos {
namespace {

void prepareStatements(pqxx::connection& conn) {
  // --- Users ---
  conn.prepare(
      "find_user_by_username",
      "SELECT id, username, password_hash, password_salt, password_iterations, role, is_active "
      "FROM users WHERE username = $1 LIMIT 1");

  conn.prepare(
      "find_session_user",
      "SELECT u.id, u.username, u.role "
      "FROM sessions s "
      "JOIN users u ON u.id = s.user_id "
      "WHERE s.token_hash = $1 "
      "AND s.revoked_at IS NULL "
      "AND s.expires_at > now() "
      "AND u.is_active = TRUE "
      "LIMIT 1");

  conn.prepare("insert_session",
                "INSERT INTO sessions (user_id, token_hash, expires_at, ip_address, user_agent) "
                "VALUES ($1, $2, now() + ($3::text || ' minutes')::interval, $4, $5)");

  conn.prepare("revoke_session",
                "UPDATE sessions SET revoked_at = now() "
                "WHERE token_hash = $1 AND revoked_at IS NULL");

  conn.prepare("cleanup_expired_sessions",
                "DELETE FROM sessions WHERE expires_at < now() OR revoked_at IS NOT NULL");

  conn.prepare("user_exists", "SELECT EXISTS (SELECT 1 FROM users WHERE username = $1)");

  conn.prepare("insert_user",
                "INSERT INTO users (username, password_hash, password_salt, password_iterations, role, is_active) "
                "VALUES ($1, $2, $3, $4, $5, $6) RETURNING id");

  conn.prepare("list_users",
                "SELECT id, username, role, is_active, created_at "
                "FROM users ORDER BY id ASC");

  // --- Categories ---
  conn.prepare("list_categories",
                "SELECT id, name, color, sort_order, is_active "
                "FROM categories WHERE is_active = TRUE ORDER BY sort_order ASC, name ASC");

  conn.prepare("insert_category",
                "INSERT INTO categories (name, color, sort_order) "
                "VALUES ($1, $2, $3) RETURNING id, name, color, sort_order, is_active");

  conn.prepare("update_category",
                "UPDATE categories SET name = $1, color = $2, sort_order = $3, updated_at = now() "
                "WHERE id = $4 AND is_active = TRUE");

  conn.prepare("delete_category",
                "UPDATE categories SET is_active = FALSE, updated_at = now() WHERE id = $1");

  // --- Products ---
  conn.prepare("list_products",
                "SELECT id, sku, name, price_cents, stock_quantity, image_url, "
                "COALESCE(category_id, 0) as category_id, COALESCE(barcode, '') as barcode, "
                "COALESCE(tax_rate_percent, 0) as tax_rate_percent, is_active "
                "FROM products WHERE is_active = TRUE ORDER BY name ASC");

  conn.prepare("insert_product",
                "INSERT INTO products (sku, name, price_cents, stock_quantity, category_id, barcode, tax_rate_percent, is_active) "
                "VALUES ($1, $2, $3, $4, NULLIF($5, 0), NULLIF($6, ''), $7, TRUE) "
                "RETURNING id, sku, name, price_cents, stock_quantity, image_url, "
                "COALESCE(category_id, 0) as category_id, COALESCE(barcode, '') as barcode, "
                "COALESCE(tax_rate_percent, 0) as tax_rate_percent, is_active");

  conn.prepare("update_product_stock",
                "UPDATE products SET stock_quantity = $1, updated_at = now() "
                "WHERE id = $2");

  conn.prepare("update_product",
                "UPDATE products SET name = $1, price_cents = $2, stock_quantity = $3, "
                "category_id = NULLIF($4, 0), barcode = NULLIF($5, ''), tax_rate_percent = $6, updated_at = now() "
                "WHERE id = $7 AND is_active = TRUE");

  conn.prepare("update_product_image",
                "UPDATE products SET image_url = $1, updated_at = now() WHERE id = $2");

  conn.prepare("find_product_by_barcode",
                "SELECT id, sku, name, price_cents, stock_quantity, image_url, "
                "COALESCE(category_id, 0) as category_id, COALESCE(barcode, '') as barcode, "
                "COALESCE(tax_rate_percent, 0) as tax_rate_percent, is_active "
                "FROM products WHERE barcode = $1 AND is_active = TRUE LIMIT 1");

  conn.prepare("search_products",
                "SELECT id, sku, name, price_cents, stock_quantity, image_url, "
                "COALESCE(category_id, 0) as category_id, COALESCE(barcode, '') as barcode, "
                "COALESCE(tax_rate_percent, 0) as tax_rate_percent, is_active "
                "FROM products WHERE is_active = TRUE AND (name ILIKE '%' || $1 || '%' OR sku ILIKE '%' || $1 || '%') "
                "ORDER BY name ASC LIMIT 50");

  conn.prepare("fetch_product_for_update",
                "SELECT id, price_cents, stock_quantity, is_active, name, "
                "COALESCE(tax_rate_percent, 0) as tax_rate_percent "
                "FROM products WHERE id = $1 FOR UPDATE");

  conn.prepare("decrement_stock",
                "UPDATE products SET stock_quantity = stock_quantity - $1, updated_at = now() "
                "WHERE id = $2");

  // --- Customers ---
  conn.prepare("list_customers",
                "SELECT id, name, phone, COALESCE(email, '') as email, loyalty_points, is_active "
                "FROM customers WHERE is_active = TRUE ORDER BY name ASC");

  conn.prepare("insert_customer",
                "INSERT INTO customers (name, phone, email) "
                "VALUES ($1, NULLIF($2, ''), NULLIF($3, '')) "
                "RETURNING id, name, COALESCE(phone, '') as phone, COALESCE(email, '') as email, loyalty_points, is_active");

  conn.prepare("update_customer",
                "UPDATE customers SET name = $1, phone = NULLIF($2, ''), email = NULLIF($3, ''), updated_at = now() "
                "WHERE id = $4 AND is_active = TRUE");

  conn.prepare("find_customer_by_phone",
                "SELECT id, name, phone, COALESCE(email, '') as email, loyalty_points, is_active "
                "FROM customers WHERE phone = $1 AND is_active = TRUE LIMIT 1");

  conn.prepare("add_loyalty_points",
                "UPDATE customers SET loyalty_points = loyalty_points + $1 WHERE id = $2");

  // --- Discounts ---
  conn.prepare("list_discounts",
                "SELECT id, name, type, value, COALESCE(promo_code, '') as promo_code, "
                "min_order_cents, COALESCE(max_uses, 0) as max_uses, used_count, is_active "
                "FROM discounts ORDER BY created_at DESC");

  conn.prepare("insert_discount",
                "INSERT INTO discounts (name, type, value, promo_code, min_order_cents, max_uses) "
                "VALUES ($1, $2, $3, NULLIF($4, ''), $5, NULLIF($6, 0)) "
                "RETURNING id, name, type, value, COALESCE(promo_code, '') as promo_code, "
                "min_order_cents, COALESCE(max_uses, 0) as max_uses, used_count, is_active");

  conn.prepare("validate_promo",
                "SELECT id, name, type, value, COALESCE(promo_code, '') as promo_code, "
                "min_order_cents, COALESCE(max_uses, 0) as max_uses, used_count, is_active "
                "FROM discounts WHERE promo_code = $1 AND is_active = TRUE "
                "AND (max_uses IS NULL OR used_count < max_uses) "
                "AND (starts_at IS NULL OR starts_at <= now()) "
                "AND (expires_at IS NULL OR expires_at > now()) "
                "LIMIT 1");

  conn.prepare("deactivate_discount",
                "UPDATE discounts SET is_active = FALSE WHERE id = $1");

  conn.prepare("increment_discount_usage",
                "UPDATE discounts SET used_count = used_count + 1 WHERE id = $1");

  conn.prepare("find_discount_by_id",
                "SELECT type, value, min_order_cents FROM discounts WHERE id = $1 AND is_active = TRUE");

  // --- Shifts ---
  conn.prepare("open_shift",
                "INSERT INTO shifts (opened_by, opening_cash_cents) "
                "VALUES ($1, $2) RETURNING id, opened_by, opening_cash_cents, opened_at");

  conn.prepare("close_shift",
                "UPDATE shifts SET closed_by = $1, closing_cash_cents = $2, expected_cash_cents = $3, "
                "notes = $4, closed_at = now() "
                "WHERE id = $5 AND closed_at IS NULL");

  conn.prepare("get_active_shift",
                "SELECT id, opened_by, COALESCE(closed_by, 0) as closed_by, opening_cash_cents, "
                "COALESCE(closing_cash_cents, 0) as closing_cash_cents, "
                "COALESCE(expected_cash_cents, 0) as expected_cash_cents, notes, "
                "opened_at::text as opened_at, COALESCE(closed_at::text, '') as closed_at "
                "FROM shifts WHERE opened_by = $1 AND closed_at IS NULL "
                "ORDER BY opened_at DESC LIMIT 1");

  conn.prepare("get_shift_by_id",
                "SELECT id, opened_by, COALESCE(closed_by, 0) as closed_by, opening_cash_cents, "
                "COALESCE(closing_cash_cents, 0) as closing_cash_cents, "
                "COALESCE(expected_cash_cents, 0) as expected_cash_cents, notes, "
                "opened_at::text as opened_at, COALESCE(closed_at::text, '') as closed_at "
                "FROM shifts WHERE id = $1");

  conn.prepare("shift_cash_sales",
                "SELECT COALESCE(SUM(sp.amount_cents), 0) as total "
                "FROM sale_payments sp "
                "JOIN sales s ON s.id = sp.sale_id "
                "WHERE s.shift_id = $1 AND sp.method = 'cash'");

  // --- Sales ---
  conn.prepare("next_receipt",
                "SELECT 'RCP-' || LPAD(nextval('receipt_seq')::text, 6, '0') as receipt_number");

  conn.prepare("insert_sale_v2",
                "INSERT INTO sales (receipt_number, sold_by_user_id, customer_id, discount_id, shift_id, "
                "subtotal_cents, tax_cents, discount_cents, total_cents, payment_method) "
                "VALUES ($1, $2, NULLIF($3, 0), NULLIF($4, 0), NULLIF($5, 0), $6, $7, $8, $9, $10) "
                "RETURNING id, receipt_number, subtotal_cents, tax_cents, discount_cents, total_cents, "
                "payment_method, created_at");

  conn.prepare("insert_sale_item_v2",
                "INSERT INTO sale_items (sale_id, product_id, product_name, quantity, unit_price_cents, tax_cents, discount_cents, line_total_cents) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");

  conn.prepare("insert_sale_payment",
                "INSERT INTO sale_payments (sale_id, method, amount_cents) VALUES ($1, $2, $3)");

  conn.prepare("insert_audit",
                "INSERT INTO audit_logs (actor_user_id, action, detail) VALUES ($1, $2, $3::jsonb)");

  conn.prepare("list_recent_sales",
                "SELECT s.id, s.receipt_number, s.subtotal_cents, s.tax_cents, s.discount_cents, "
                "s.total_cents, s.payment_method, s.created_at, u.username, "
                "COALESCE(s.customer_id, 0) as customer_id "
                "FROM sales s "
                "JOIN users u ON u.id = s.sold_by_user_id "
                "ORDER BY s.created_at DESC "
                "LIMIT $1");

  conn.prepare("get_sale_by_id",
                "SELECT s.id, s.receipt_number, s.subtotal_cents, s.tax_cents, s.discount_cents, "
                "s.total_cents, s.payment_method, s.created_at, u.username, "
                "COALESCE(s.customer_id, 0) as customer_id "
                "FROM sales s "
                "JOIN users u ON u.id = s.sold_by_user_id "
                "WHERE s.id = $1");

  conn.prepare("get_sale_items",
                "SELECT product_id, product_name, quantity, unit_price_cents, tax_cents, discount_cents, line_total_cents "
                "FROM sale_items WHERE sale_id = $1 ORDER BY id ASC");

  conn.prepare("get_sale_payments",
                "SELECT method, amount_cents FROM sale_payments WHERE sale_id = $1 ORDER BY id ASC");

  // --- Refunds ---
  conn.prepare("insert_refund",
                "INSERT INTO refunds (sale_id, refunded_by, total_cents, reason) "
                "VALUES ($1, $2, $3, $4) RETURNING id, created_at");

  conn.prepare("insert_refund_item",
                "INSERT INTO refund_items (refund_id, product_id, quantity, unit_price_cents, line_total_cents) "
                "VALUES ($1, $2, $3, $4, $5)");

  conn.prepare("increment_stock",
                "UPDATE products SET stock_quantity = stock_quantity + $1, updated_at = now() WHERE id = $2");

  conn.prepare("list_refunds",
                "SELECT r.id, r.sale_id, s.receipt_number, r.refunded_by, r.total_cents, r.reason, r.created_at "
                "FROM refunds r "
                "JOIN sales s ON s.id = r.sale_id "
                "ORDER BY r.created_at DESC LIMIT $1");

  // --- Reports ---
  conn.prepare("sales_report",
                "SELECT d::date::text as date, "
                "COALESCE(COUNT(s.id), 0) as sale_count, "
                "COALESCE(SUM(s.total_cents), 0) as revenue_cents, "
                "COALESCE(SUM(CASE WHEN s.payment_method = 'cash' THEN s.total_cents ELSE 0 END), 0) as cash_cents, "
                "COALESCE(SUM(CASE WHEN s.payment_method = 'card' THEN s.total_cents ELSE 0 END), 0) as card_cents, "
                "COALESCE(SUM(CASE WHEN s.payment_method = 'transfer' THEN s.total_cents ELSE 0 END), 0) as transfer_cents "
                "FROM generate_series($1::date, $2::date, '1 day'::interval) d "
                "LEFT JOIN sales s ON s.created_at::date = d::date "
                "GROUP BY d::date ORDER BY d::date ASC");

  conn.prepare("refund_totals_by_date",
                "SELECT COALESCE(SUM(r.total_cents), 0) as refund_cents "
                "FROM refunds r WHERE r.created_at::date BETWEEN $1::date AND $2::date");

  conn.prepare("top_products",
                "SELECT si.product_id, si.product_name, "
                "SUM(si.quantity) as total_quantity, SUM(si.line_total_cents) as total_revenue_cents "
                "FROM sale_items si "
                "JOIN sales s ON s.id = si.sale_id "
                "WHERE s.created_at::date BETWEEN $1::date AND $2::date "
                "GROUP BY si.product_id, si.product_name "
                "ORDER BY total_quantity DESC LIMIT $3");
}

}  // namespace (end anonymous for pool classes)

std::unique_ptr<pqxx::connection> createConnection(const std::string& connection_string) {
  auto conn = std::make_unique<pqxx::connection>(connection_string);
  if (!conn->is_open()) {
    throw std::runtime_error("Failed to connect to PostgreSQL.");
  }
  prepareStatements(*conn);
  return conn;
}

// Simple connection pool to avoid opening a new connection per request.
class ConnectionPool {
 public:
  ConnectionPool(std::string connection_string, int pool_size)
      : connection_string_(std::move(connection_string)) {
    for (int i = 0; i < pool_size; ++i) {
      pool_.push(createConnection(connection_string_));
    }
  }

  std::unique_ptr<pqxx::connection> acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return !pool_.empty(); });
    auto conn = std::move(pool_.front());
    pool_.pop();
    // Verify connection is still alive
    if (!conn->is_open()) {
      conn = createConnection(connection_string_);
    }
    return conn;
  }

  void release(std::unique_ptr<pqxx::connection> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(std::move(conn));
    cond_.notify_one();
  }

 private:
  std::string connection_string_;
  std::queue<std::unique_ptr<pqxx::connection>> pool_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

// RAII guard that returns the connection to the pool on destruction.
class PooledConnection {
 public:
  PooledConnection(ConnectionPool& pool) : pool_(pool), conn_(pool.acquire()) {}
  ~PooledConnection() { pool_.release(std::move(conn_)); }

  PooledConnection(const PooledConnection&) = delete;
  PooledConnection& operator=(const PooledConnection&) = delete;

  pqxx::connection& operator*() { return *conn_; }
  pqxx::connection* operator->() { return conn_.get(); }

 private:
  ConnectionPool& pool_;
  std::unique_ptr<pqxx::connection> conn_;
};

namespace {

ProductRecord rowToProduct(const pqxx::row& row) {
  return ProductRecord{
      row["id"].as<int>(),
      row["sku"].as<std::string>(),
      row["name"].as<std::string>(),
      row["price_cents"].as<int>(),
      row["stock_quantity"].as<int>(),
      row["image_url"].as<std::string>(),
      row["category_id"].as<int>(),
      row["barcode"].as<std::string>(),
      row["tax_rate_percent"].as<double>(),
      row["is_active"].as<bool>(),
  };
}

CustomerRecord rowToCustomer(const pqxx::row& row) {
  return CustomerRecord{
      row["id"].as<int>(),
      row["name"].as<std::string>(),
      row["phone"].as<std::string>(),
      row["email"].as<std::string>(),
      row["loyalty_points"].as<int>(),
      row["is_active"].as<bool>(),
  };
}

DiscountRecord rowToDiscount(const pqxx::row& row) {
  return DiscountRecord{
      row["id"].as<int>(),
      row["name"].as<std::string>(),
      row["type"].as<std::string>(),
      row["value"].as<int>(),
      row["promo_code"].as<std::string>(),
      row["min_order_cents"].as<int>(),
      row["max_uses"].as<int>(),
      row["used_count"].as<int>(),
      row["is_active"].as<bool>(),
  };
}

ShiftRecord rowToShift(const pqxx::row& row) {
  return ShiftRecord{
      row["id"].as<int>(),
      row["opened_by"].as<int>(),
      row["closed_by"].as<int>(),
      row["opening_cash_cents"].as<int>(),
      row["closing_cash_cents"].as<int>(),
      row["expected_cash_cents"].as<int>(),
      row["notes"].as<std::string>(),
      row["opened_at"].as<std::string>(),
      row["closed_at"].as<std::string>(),
  };
}

}  // namespace

Database::Database(std::string connection_string, int pool_size)
    : connection_string_(std::move(connection_string)),
      pool_(std::make_shared<ConnectionPool>(connection_string_, pool_size)) {}

void Database::verifyConnectivity() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  tx.exec("SELECT 1");
}

// ========================================
// Users
// ========================================

std::optional<UserRecord> Database::findUserByUsername(const std::string& username) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("find_user_by_username", username);
  if (rows.empty()) return std::nullopt;
  const auto row = rows[0];
  return UserRecord{
      row["id"].as<int>(), row["username"].as<std::string>(),
      row["password_hash"].as<std::string>(), row["password_salt"].as<std::string>(),
      row["password_iterations"].as<int>(), row["role"].as<std::string>(),
      row["is_active"].as<bool>(),
  };
}

std::optional<AuthUser> Database::findActiveSessionUser(const std::string& token_hash) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("find_session_user", token_hash);
  if (rows.empty()) return std::nullopt;
  const auto row = rows[0];
  return AuthUser{row["id"].as<int>(), row["username"].as<std::string>(), row["role"].as<std::string>()};
}

void Database::createSession(int user_id, const std::string& token_hash, int session_ttl_minutes,
                             const std::string& client_ip, const std::string& user_agent) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  tx.exec_prepared("insert_session", user_id, token_hash, session_ttl_minutes, client_ip, user_agent);
  tx.commit();
}

void Database::revokeSession(const std::string& token_hash) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  tx.exec_prepared("revoke_session", token_hash);
  tx.commit();
}

void Database::cleanupExpiredSessions() const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  tx.exec_prepared("cleanup_expired_sessions");
  tx.commit();
}

bool Database::userExists(const std::string& username) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const auto row = tx.exec_prepared1("user_exists", username);
  return row[0].as<bool>();
}

void Database::createUser(const std::string& username, const std::string& password_hash,
                          const std::string& password_salt, int password_iterations,
                          const std::string& role, bool is_active) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_user", username, password_hash, password_salt, password_iterations, role, is_active);
  const int user_id = row["id"].as<int>();
  nlohmann::json detail = {{"user_id", user_id}, {"username", username}, {"role", role}, {"is_active", is_active}, {"source", "bootstrap"}};
  tx.exec_prepared("insert_audit", user_id, "user.created", detail.dump());
  tx.commit();
}

std::vector<UserRecord> Database::listUsers() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_users");
  std::vector<UserRecord> users;
  users.reserve(rows.size());
  for (const auto& row : rows) {
    users.push_back(UserRecord{
        row["id"].as<int>(), row["username"].as<std::string>(),
        "", "", 0, row["role"].as<std::string>(), row["is_active"].as<bool>(),
    });
  }
  return users;
}

int Database::createUserByAdmin(const std::string& username, const std::string& password_hash,
                                const std::string& password_salt, int password_iterations,
                                const std::string& role, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_user", username, password_hash, password_salt, password_iterations, role, true);
  const int user_id = row["id"].as<int>();
  nlohmann::json detail = {{"user_id", user_id}, {"username", username}, {"role", role}, {"source", "admin"}};
  tx.exec_prepared("insert_audit", actor_user_id, "user.created", detail.dump());
  tx.commit();
  return user_id;
}

// ========================================
// Categories
// ========================================

std::vector<CategoryRecord> Database::listCategories() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_categories");
  std::vector<CategoryRecord> cats;
  cats.reserve(rows.size());
  for (const auto& row : rows) {
    cats.push_back(CategoryRecord{
        row["id"].as<int>(), row["name"].as<std::string>(),
        row["color"].as<std::string>(), row["sort_order"].as<int>(),
        row["is_active"].as<bool>(),
    });
  }
  return cats;
}

CategoryRecord Database::createCategory(const std::string& name, const std::string& color,
                                        int sort_order, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_category", name, color, sort_order);
  nlohmann::json detail = {{"category_id", row["id"].as<int>()}, {"name", name}};
  tx.exec_prepared("insert_audit", actor_user_id, "category.created", detail.dump());
  tx.commit();
  return CategoryRecord{
      row["id"].as<int>(), row["name"].as<std::string>(),
      row["color"].as<std::string>(), row["sort_order"].as<int>(),
      row["is_active"].as<bool>(),
  };
}

bool Database::updateCategory(int id, const std::string& name, const std::string& color,
                              int sort_order, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("update_category", name, color, sort_order, id);
  const bool updated = r.affected_rows() == 1;
  if (updated) {
    nlohmann::json detail = {{"category_id", id}, {"name", name}};
    tx.exec_prepared("insert_audit", actor_user_id, "category.updated", detail.dump());
  }
  tx.commit();
  return updated;
}

bool Database::deleteCategory(int id, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("delete_category", id);
  const bool deleted = r.affected_rows() == 1;
  if (deleted) {
    nlohmann::json detail = {{"category_id", id}};
    tx.exec_prepared("insert_audit", actor_user_id, "category.deleted", detail.dump());
  }
  tx.commit();
  return deleted;
}

// ========================================
// Products
// ========================================

std::vector<ProductRecord> Database::listActiveProducts() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_products");
  std::vector<ProductRecord> products;
  products.reserve(rows.size());
  for (const auto& row : rows) {
    products.push_back(rowToProduct(row));
  }
  return products;
}

ProductRecord Database::createProduct(const std::string& sku, const std::string& name,
                                      int price_cents, int stock_quantity,
                                      int category_id, const std::string& barcode,
                                      double tax_rate_percent, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_product", sku, name, price_cents, stock_quantity,
                                     category_id, barcode, tax_rate_percent);
  nlohmann::json detail = {
      {"product_id", row["id"].as<int>()}, {"sku", sku}, {"name", name},
      {"price_cents", price_cents}, {"stock_quantity", stock_quantity},
  };
  tx.exec_prepared("insert_audit", actor_user_id, "product.created", detail.dump());
  tx.commit();
  return rowToProduct(row);
}

bool Database::updateProductStock(int product_id, int stock_quantity, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("update_product_stock", stock_quantity, product_id);
  const bool updated = r.affected_rows() == 1;
  if (updated) {
    nlohmann::json detail = {{"product_id", product_id}, {"stock_quantity", stock_quantity}};
    tx.exec_prepared("insert_audit", actor_user_id, "product.stock_updated", detail.dump());
  }
  tx.commit();
  return updated;
}

bool Database::updateProduct(int product_id, const std::string& name, int price_cents,
                             int stock_quantity, int category_id, const std::string& barcode,
                             double tax_rate_percent, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("update_product", name, price_cents, stock_quantity,
                                          category_id, barcode, tax_rate_percent, product_id);
  const bool updated = r.affected_rows() == 1;
  if (updated) {
    nlohmann::json detail = {
        {"product_id", product_id}, {"name", name}, {"price_cents", price_cents},
        {"stock_quantity", stock_quantity},
    };
    tx.exec_prepared("insert_audit", actor_user_id, "product.updated", detail.dump());
  }
  tx.commit();
  return updated;
}

bool Database::updateProductImage(int product_id, const std::string& image_url, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("update_product_image", image_url, product_id);
  const bool updated = r.affected_rows() == 1;
  if (updated) {
    nlohmann::json detail = {{"product_id", product_id}, {"image_url", image_url}};
    tx.exec_prepared("insert_audit", actor_user_id, "product.image_updated", detail.dump());
  }
  tx.commit();
  return updated;
}

std::optional<ProductRecord> Database::findProductByBarcode(const std::string& barcode) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("find_product_by_barcode", barcode);
  if (rows.empty()) return std::nullopt;
  return rowToProduct(rows[0]);
}

std::vector<ProductRecord> Database::searchProducts(const std::string& query) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("search_products", query);
  std::vector<ProductRecord> products;
  products.reserve(rows.size());
  for (const auto& row : rows) {
    products.push_back(rowToProduct(row));
  }
  return products;
}

// ========================================
// Customers
// ========================================

std::vector<CustomerRecord> Database::listCustomers() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_customers");
  std::vector<CustomerRecord> customers;
  customers.reserve(rows.size());
  for (const auto& row : rows) {
    customers.push_back(rowToCustomer(row));
  }
  return customers;
}

CustomerRecord Database::createCustomer(const std::string& name, const std::string& phone,
                                        const std::string& email, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_customer", name, phone, email);
  nlohmann::json detail = {{"customer_id", row["id"].as<int>()}, {"name", name}, {"phone", phone}};
  tx.exec_prepared("insert_audit", actor_user_id, "customer.created", detail.dump());
  tx.commit();
  return rowToCustomer(row);
}

bool Database::updateCustomer(int id, const std::string& name, const std::string& phone,
                              const std::string& email, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("update_customer", name, phone, email, id);
  const bool updated = r.affected_rows() == 1;
  if (updated) {
    nlohmann::json detail = {{"customer_id", id}, {"name", name}};
    tx.exec_prepared("insert_audit", actor_user_id, "customer.updated", detail.dump());
  }
  tx.commit();
  return updated;
}

std::optional<CustomerRecord> Database::findCustomerByPhone(const std::string& phone) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("find_customer_by_phone", phone);
  if (rows.empty()) return std::nullopt;
  return rowToCustomer(rows[0]);
}

bool Database::addLoyaltyPoints(int customer_id, int points) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("add_loyalty_points", points, customer_id);
  tx.commit();
  return r.affected_rows() == 1;
}

// ========================================
// Discounts
// ========================================

std::vector<DiscountRecord> Database::listDiscounts() const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_discounts");
  std::vector<DiscountRecord> discounts;
  discounts.reserve(rows.size());
  for (const auto& row : rows) {
    discounts.push_back(rowToDiscount(row));
  }
  return discounts;
}

DiscountRecord Database::createDiscount(const std::string& name, const std::string& type,
                                        int value, const std::string& promo_code,
                                        int min_order_cents, int max_uses,
                                        int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("insert_discount", name, type, value, promo_code, min_order_cents, max_uses);
  nlohmann::json detail = {{"discount_id", row["id"].as<int>()}, {"name", name}, {"type", type}, {"value", value}};
  tx.exec_prepared("insert_audit", actor_user_id, "discount.created", detail.dump());
  tx.commit();
  return rowToDiscount(row);
}

std::optional<DiscountRecord> Database::validatePromoCode(const std::string& code) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("validate_promo", code);
  if (rows.empty()) return std::nullopt;
  return rowToDiscount(rows[0]);
}

bool Database::deactivateDiscount(int id, int actor_user_id) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const pqxx::result r = tx.exec_prepared("deactivate_discount", id);
  const bool ok = r.affected_rows() == 1;
  if (ok) {
    nlohmann::json detail = {{"discount_id", id}};
    tx.exec_prepared("insert_audit", actor_user_id, "discount.deactivated", detail.dump());
  }
  tx.commit();
  return ok;
}

// ========================================
// Shifts
// ========================================

ShiftRecord Database::openShift(int user_id, int opening_cash_cents) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);
  const auto row = tx.exec_prepared1("open_shift", user_id, opening_cash_cents);
  nlohmann::json detail = {{"shift_id", row["id"].as<int>()}, {"opening_cash_cents", opening_cash_cents}};
  tx.exec_prepared("insert_audit", user_id, "shift.opened", detail.dump());
  tx.commit();
  return ShiftRecord{
      row["id"].as<int>(), row["opened_by"].as<int>(), 0,
      row["opening_cash_cents"].as<int>(), 0, 0, "",
      std::string(row["opened_at"].c_str()), "",
  };
}

bool Database::closeShift(int shift_id, int user_id, int closing_cash_cents, const std::string& notes) const {
  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);

  // Calculate expected cash = opening + cash sales during shift
  const auto shift_rows = tx.exec_prepared("get_shift_by_id", shift_id);
  if (shift_rows.empty()) return false;
  const int opening = shift_rows[0]["opening_cash_cents"].as<int>();

  const auto cash_row = tx.exec_prepared1("shift_cash_sales", shift_id);
  const int cash_sales = cash_row[0].as<int>();
  const int expected = opening + cash_sales;

  const pqxx::result r = tx.exec_prepared("close_shift", user_id, closing_cash_cents, expected, notes, shift_id);
  const bool ok = r.affected_rows() == 1;
  if (ok) {
    nlohmann::json detail = {
        {"shift_id", shift_id}, {"closing_cash_cents", closing_cash_cents},
        {"expected_cash_cents", expected}, {"difference", closing_cash_cents - expected},
    };
    tx.exec_prepared("insert_audit", user_id, "shift.closed", detail.dump());
  }
  tx.commit();
  return ok;
}

std::optional<ShiftRecord> Database::getActiveShift(int user_id) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("get_active_shift", user_id);
  if (rows.empty()) return std::nullopt;
  return rowToShift(rows[0]);
}

std::optional<ShiftRecord> Database::getShiftById(int shift_id) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("get_shift_by_id", shift_id);
  if (rows.empty()) return std::nullopt;
  return rowToShift(rows[0]);
}

// ========================================
// Sales (V2 – with tax, discount, split payment)
// ========================================

SaleRecord Database::createSaleV2(const SaleCreateInput& input) const {
  if (input.items.empty()) {
    throw std::runtime_error("At least one sale item is required.");
  }

  struct ComputedLine {
    int product_id;
    std::string product_name;
    int quantity;
    int unit_price_cents;
    int tax_cents;
    int line_total_cents;
  };

  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);

  std::vector<ComputedLine> computed;
  computed.reserve(input.items.size());

  int subtotal_cents = 0;
  int total_tax_cents = 0;

  for (const auto& item : input.items) {
    const pqxx::result product_rows = tx.exec_prepared("fetch_product_for_update", item.product_id);
    if (product_rows.empty()) throw std::runtime_error("Product not found.");

    const auto row = product_rows[0];
    if (!row["is_active"].as<bool>()) throw std::runtime_error("Product is inactive.");

    const int stock = row["stock_quantity"].as<int>();
    const int unit_price = row["price_cents"].as<int>();
    const std::string name = row["name"].as<std::string>();
    const double tax_rate = row["tax_rate_percent"].as<double>();

    if (item.quantity > stock) {
      throw std::runtime_error("Insufficient stock for " + name + ".");
    }

    const int line_subtotal = unit_price * item.quantity;
    const int line_tax = static_cast<int>(std::round(line_subtotal * tax_rate / 100.0));

    subtotal_cents += line_subtotal;
    total_tax_cents += line_tax;

    computed.push_back(ComputedLine{item.product_id, name, item.quantity, unit_price, line_tax, line_subtotal + line_tax});
  }

  // Apply discount
  int discount_cents = 0;
  if (input.discount_id > 0) {
    // Look up via validate_promo won't work here since we have discount_id, not code.
    // Use a direct query.
    const pqxx::result dr = tx.exec_prepared("find_discount_by_id", input.discount_id);
    if (!dr.empty()) {
      const std::string dtype = dr[0]["type"].as<std::string>();
      const int dvalue = dr[0]["value"].as<int>();
      const int min_order = dr[0]["min_order_cents"].as<int>();
      if (subtotal_cents >= min_order) {
        if (dtype == "percent") {
          discount_cents = static_cast<int>(std::round(subtotal_cents * dvalue / 100.0));
        } else {
          discount_cents = dvalue;
        }
        if (discount_cents > subtotal_cents + total_tax_cents) {
          discount_cents = subtotal_cents + total_tax_cents;
        }
        tx.exec_prepared("increment_discount_usage", input.discount_id);
      }
    }
  }

  const int total_cents = subtotal_cents + total_tax_cents - discount_cents;
  if (total_cents <= 0) throw std::runtime_error("Total must be positive after discount.");

  // Generate receipt number
  const auto receipt_row = tx.exec_prepared1("next_receipt");
  const std::string receipt_number = receipt_row[0].as<std::string>();

  // Insert sale
  const auto sale_row = tx.exec_prepared1(
      "insert_sale_v2", receipt_number, input.sold_by_user_id, input.customer_id,
      input.discount_id, input.shift_id, subtotal_cents, total_tax_cents, discount_cents,
      total_cents, input.payment_method);
  const int sale_id = sale_row["id"].as<int>();

  // Insert line items
  nlohmann::json audit_items = nlohmann::json::array();
  for (const auto& line : computed) {
    tx.exec_prepared("insert_sale_item_v2", sale_id, line.product_id, line.product_name,
                     line.quantity, line.unit_price_cents, line.tax_cents, 0, line.line_total_cents);
    tx.exec_prepared("decrement_stock", line.quantity, line.product_id);
    audit_items.push_back({
        {"product_id", line.product_id}, {"name", line.product_name},
        {"quantity", line.quantity}, {"unit_price_cents", line.unit_price_cents},
        {"line_total_cents", line.line_total_cents},
    });
  }

  // Insert payments
  if (input.payment_method == "split" && !input.payments.empty()) {
    for (const auto& p : input.payments) {
      tx.exec_prepared("insert_sale_payment", sale_id, p.method, p.amount_cents);
    }
  } else {
    // Single payment = entire total
    tx.exec_prepared("insert_sale_payment", sale_id, input.payment_method, total_cents);
  }

  // Loyalty points (1 point per 100 cents spent)
  if (input.customer_id > 0) {
    const int points = total_cents / 100;
    if (points > 0) {
      tx.exec_prepared("add_loyalty_points", points, input.customer_id);
    }
  }

  // Audit
  nlohmann::json detail = {
      {"sale_id", sale_id}, {"receipt_number", receipt_number},
      {"payment_method", input.payment_method}, {"subtotal_cents", subtotal_cents},
      {"tax_cents", total_tax_cents}, {"discount_cents", discount_cents},
      {"total_cents", total_cents}, {"items", audit_items},
  };
  tx.exec_prepared("insert_audit", input.sold_by_user_id, "sale.created", detail.dump());

  tx.commit();

  return SaleRecord{
      sale_id, receipt_number, subtotal_cents, total_tax_cents, discount_cents,
      total_cents, sale_row["payment_method"].as<std::string>(),
      std::string(sale_row["created_at"].c_str()), std::string(), input.customer_id,
  };
}

std::vector<SaleRecord> Database::listRecentSales(int limit) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_recent_sales", limit);
  std::vector<SaleRecord> sales;
  sales.reserve(rows.size());
  for (const auto& row : rows) {
    sales.push_back(SaleRecord{
        row["id"].as<int>(), row["receipt_number"].as<std::string>(),
        row["subtotal_cents"].as<int>(), row["tax_cents"].as<int>(),
        row["discount_cents"].as<int>(), row["total_cents"].as<int>(),
        row["payment_method"].as<std::string>(),
        std::string(row["created_at"].c_str()),
        row["username"].as<std::string>(),
        row["customer_id"].as<int>(),
    });
  }
  return sales;
}

std::optional<SaleDetail> Database::getSaleDetail(int sale_id) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);

  const pqxx::result sale_rows = tx.exec_prepared("get_sale_by_id", sale_id);
  if (sale_rows.empty()) return std::nullopt;

  const auto& sr = sale_rows[0];
  SaleRecord sale{
      sr["id"].as<int>(), sr["receipt_number"].as<std::string>(),
      sr["subtotal_cents"].as<int>(), sr["tax_cents"].as<int>(),
      sr["discount_cents"].as<int>(), sr["total_cents"].as<int>(),
      sr["payment_method"].as<std::string>(),
      std::string(sr["created_at"].c_str()),
      sr["username"].as<std::string>(),
      sr["customer_id"].as<int>(),
  };

  const pqxx::result item_rows = tx.exec_prepared("get_sale_items", sale_id);
  std::vector<SaleItemDetail> items;
  items.reserve(item_rows.size());
  for (const auto& row : item_rows) {
    items.push_back(SaleItemDetail{
        row["product_id"].as<int>(), row["product_name"].as<std::string>(),
        row["quantity"].as<int>(), row["unit_price_cents"].as<int>(),
        row["tax_cents"].as<int>(), row["discount_cents"].as<int>(),
        row["line_total_cents"].as<int>(),
    });
  }

  const pqxx::result pay_rows = tx.exec_prepared("get_sale_payments", sale_id);
  std::vector<SalePaymentDetail> payments;
  payments.reserve(pay_rows.size());
  for (const auto& row : pay_rows) {
    payments.push_back(SalePaymentDetail{row["method"].as<std::string>(), row["amount_cents"].as<int>()});
  }

  return SaleDetail{sale, items, payments};
}

// ========================================
// Refunds
// ========================================

RefundRecord Database::createRefund(int sale_id, int refunded_by,
                                    const std::vector<RefundItemInput>& items,
                                    const std::string& reason) const {
  if (items.empty()) throw std::runtime_error("At least one refund item is required.");

  PooledConnection conn(*pool_);
  pqxx::work tx(*conn);

  // Verify sale exists
  const pqxx::result sale_rows = tx.exec_prepared("get_sale_by_id", sale_id);
  if (sale_rows.empty()) throw std::runtime_error("Sale not found.");

  // Load sale items for price lookup
  const pqxx::result si_rows = tx.exec_prepared("get_sale_items", sale_id);
  std::map<int, int> item_prices;
  std::map<int, int> item_quantities;
  for (const auto& row : si_rows) {
    item_prices[row["product_id"].as<int>()] = row["unit_price_cents"].as<int>();
    item_quantities[row["product_id"].as<int>()] = row["quantity"].as<int>();
  }

  int total_refund = 0;
  struct RefundLine {
    int product_id;
    int quantity;
    int unit_price;
    int line_total;
  };
  std::vector<RefundLine> lines;

  for (const auto& item : items) {
    auto it = item_prices.find(item.product_id);
    if (it == item_prices.end()) {
      throw std::runtime_error("Product not in original sale.");
    }
    const int unit_price = it->second;
    const int line_total = unit_price * item.quantity;
    total_refund += line_total;
    lines.push_back(RefundLine{item.product_id, item.quantity, unit_price, line_total});
  }

  const auto refund_row = tx.exec_prepared1("insert_refund", sale_id, refunded_by, total_refund, reason);
  const int refund_id = refund_row["id"].as<int>();

  for (const auto& line : lines) {
    tx.exec_prepared("insert_refund_item", refund_id, line.product_id, line.quantity, line.unit_price, line.line_total);
    tx.exec_prepared("increment_stock", line.quantity, line.product_id);
  }

  nlohmann::json detail = {{"refund_id", refund_id}, {"sale_id", sale_id}, {"total_cents", total_refund}, {"reason", reason}};
  tx.exec_prepared("insert_audit", refunded_by, "refund.created", detail.dump());

  tx.commit();

  return RefundRecord{
      refund_id, sale_id, sale_rows[0]["receipt_number"].as<std::string>(),
      refunded_by, total_refund, reason,
      std::string(refund_row["created_at"].c_str()),
  };
}

std::vector<RefundRecord> Database::listRefunds(int limit) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("list_refunds", limit);
  std::vector<RefundRecord> refunds;
  refunds.reserve(rows.size());
  for (const auto& row : rows) {
    refunds.push_back(RefundRecord{
        row["id"].as<int>(), row["sale_id"].as<int>(),
        row["receipt_number"].as<std::string>(),
        row["refunded_by"].as<int>(), row["total_cents"].as<int>(),
        row["reason"].as<std::string>(),
        std::string(row["created_at"].c_str()),
    });
  }
  return refunds;
}

// ========================================
// Reports
// ========================================

std::vector<SalesReportRow> Database::getSalesReport(const std::string& start_date,
                                                     const std::string& end_date) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("sales_report", start_date, end_date);

  // Get refund total for the period
  const auto refund_row = tx.exec_prepared1("refund_totals_by_date", start_date, end_date);
  const int total_refunds = refund_row[0].as<int>();

  std::vector<SalesReportRow> report;
  report.reserve(rows.size());
  for (const auto& row : rows) {
    report.push_back(SalesReportRow{
        row["date"].as<std::string>(),
        row["sale_count"].as<int>(),
        row["revenue_cents"].as<int>(),
        row["cash_cents"].as<int>(),
        row["card_cents"].as<int>(),
        row["transfer_cents"].as<int>(),
        0,
    });
  }
  // Put total refunds on the last row for convenience
  if (!report.empty()) {
    report.back().refund_cents = total_refunds;
  }
  return report;
}

std::vector<TopProduct> Database::getTopProducts(int limit, const std::string& start_date,
                                                 const std::string& end_date) const {
  PooledConnection conn(*pool_);
  pqxx::read_transaction tx(*conn);
  const pqxx::result rows = tx.exec_prepared("top_products", start_date, end_date, limit);
  std::vector<TopProduct> products;
  products.reserve(rows.size());
  for (const auto& row : rows) {
    products.push_back(TopProduct{
        row["product_id"].as<int>(),
        row["product_name"].as<std::string>(),
        row["total_quantity"].as<int>(),
        row["total_revenue_cents"].as<int>(),
    });
  }
  return products;
}

}  // namespace pos