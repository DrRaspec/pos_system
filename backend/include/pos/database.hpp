#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pos {

struct UserRecord {
  int id;
  std::string username;
  std::string password_hash;
  std::string password_salt;
  int password_iterations;
  std::string role;
  bool is_active;
};

struct AuthUser {
  int user_id;
  std::string username;
  std::string role;
};

struct ProductRecord {
  int id;
  std::string sku;
  std::string name;
  int price_cents;
  int stock_quantity;
  std::string image_url;
  bool is_active;
};

struct SaleItemRequest {
  int product_id;
  int quantity;
};

struct SaleRecord {
  int id;
  int total_cents;
  std::string payment_method;
  std::string created_at;
  std::string sold_by;
};

class Database {
 public:
  explicit Database(std::string connection_string);

  void verifyConnectivity() const;

  std::optional<UserRecord> findUserByUsername(const std::string& username) const;
  std::optional<AuthUser> findActiveSessionUser(const std::string& token_hash) const;

  void createSession(int user_id,
                     const std::string& token_hash,
                     int session_ttl_minutes,
                     const std::string& client_ip,
                     const std::string& user_agent) const;
  void revokeSession(const std::string& token_hash) const;

  std::vector<ProductRecord> listActiveProducts() const;
  ProductRecord createProduct(const std::string& sku,
                              const std::string& name,
                              int price_cents,
                              int stock_quantity,
                              int actor_user_id) const;
  bool updateProductStock(int product_id, int stock_quantity, int actor_user_id) const;

  SaleRecord createSale(int sold_by_user_id,
                        const std::string& payment_method,
                        const std::vector<SaleItemRequest>& items) const;
  std::vector<SaleRecord> listRecentSales(int limit) const;

  bool userExists(const std::string& username) const;
  void createUser(const std::string& username,
                  const std::string& password_hash,
                  const std::string& password_salt,
                  int password_iterations,
                  const std::string& role,
                  bool is_active) const;

  std::vector<UserRecord> listUsers() const;
  int createUserByAdmin(const std::string& username, const std::string& password_hash,
                        const std::string& password_salt, int password_iterations,
                        const std::string& role, int actor_user_id) const;

  bool updateProductImage(int product_id, const std::string& image_url, int actor_user_id) const;

 private:
  std::string connection_string_;
};

}  // namespace pos