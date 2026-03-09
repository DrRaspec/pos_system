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

struct CategoryRecord {
  int id;
  std::string name;
  std::string color;
  int sort_order;
  bool is_active;
};

struct ProductRecord {
  int id;
  std::string sku;
  std::string name;
  int price_cents;
  int stock_quantity;
  std::string image_url;
  int category_id;
  std::string barcode;
  double tax_rate_percent;
  bool is_active;
};

struct CustomerRecord {
  int id;
  std::string name;
  std::string phone;
  std::string email;
  int loyalty_points;
  bool is_active;
};

struct DiscountRecord {
  int id;
  std::string name;
  std::string type;   // "percent" | "fixed"
  int value;
  std::string promo_code;
  int min_order_cents;
  int max_uses;
  int used_count;
  bool is_active;
};

struct ShiftRecord {
  int id;
  int opened_by;
  int closed_by;
  int opening_cash_cents;
  int closing_cash_cents;
  int expected_cash_cents;
  std::string notes;
  std::string opened_at;
  std::string closed_at;
};

struct SaleItemRequest {
  int product_id;
  int quantity;
};

struct SalePaymentInput {
  std::string method;  // "cash" | "card" | "transfer"
  int amount_cents;
};

struct SaleRecord {
  int id;
  std::string receipt_number;
  int subtotal_cents;
  int tax_cents;
  int discount_cents;
  int total_cents;
  std::string payment_method;
  std::string created_at;
  std::string sold_by;
  int customer_id;
};

struct SaleItemDetail {
  int product_id;
  std::string product_name;
  int quantity;
  int unit_price_cents;
  int tax_cents;
  int discount_cents;
  int line_total_cents;
};

struct SalePaymentDetail {
  std::string method;
  int amount_cents;
};

struct SaleDetail {
  SaleRecord sale;
  std::vector<SaleItemDetail> items;
  std::vector<SalePaymentDetail> payments;
};

struct RefundRecord {
  int id;
  int sale_id;
  std::string receipt_number;
  int refunded_by;
  int total_cents;
  std::string reason;
  std::string created_at;
};

struct RefundItemInput {
  int product_id;
  int quantity;
};

struct SalesReportRow {
  std::string date;
  int sale_count;
  int revenue_cents;
  int cash_cents;
  int card_cents;
  int transfer_cents;
  int refund_cents;
};

struct TopProduct {
  int product_id;
  std::string product_name;
  int total_quantity;
  int total_revenue_cents;
};

struct SaleCreateInput {
  int sold_by_user_id;
  std::string payment_method;
  std::vector<SaleItemRequest> items;
  int customer_id;       // 0 = no customer
  int discount_id;       // 0 = no discount
  int shift_id;          // 0 = no shift
  std::vector<SalePaymentInput> payments; // for split payments
};

class Database {
 public:
  explicit Database(std::string connection_string);

  void verifyConnectivity() const;

  // --- Users ---
  std::optional<UserRecord> findUserByUsername(const std::string& username) const;
  std::optional<AuthUser> findActiveSessionUser(const std::string& token_hash) const;
  void createSession(int user_id, const std::string& token_hash, int session_ttl_minutes,
                     const std::string& client_ip, const std::string& user_agent) const;
  void revokeSession(const std::string& token_hash) const;
  bool userExists(const std::string& username) const;
  void createUser(const std::string& username, const std::string& password_hash,
                  const std::string& password_salt, int password_iterations,
                  const std::string& role, bool is_active) const;
  std::vector<UserRecord> listUsers() const;
  int createUserByAdmin(const std::string& username, const std::string& password_hash,
                        const std::string& password_salt, int password_iterations,
                        const std::string& role, int actor_user_id) const;

  // --- Categories ---
  std::vector<CategoryRecord> listCategories() const;
  CategoryRecord createCategory(const std::string& name, const std::string& color,
                                int sort_order, int actor_user_id) const;
  bool updateCategory(int id, const std::string& name, const std::string& color,
                      int sort_order, int actor_user_id) const;
  bool deleteCategory(int id, int actor_user_id) const;

  // --- Products ---
  std::vector<ProductRecord> listActiveProducts() const;
  ProductRecord createProduct(const std::string& sku, const std::string& name,
                              int price_cents, int stock_quantity,
                              int category_id, const std::string& barcode,
                              double tax_rate_percent, int actor_user_id) const;
  bool updateProductStock(int product_id, int stock_quantity, int actor_user_id) const;
  bool updateProduct(int product_id, const std::string& name, int price_cents,
                     int stock_quantity, int category_id, const std::string& barcode,
                     double tax_rate_percent, int actor_user_id) const;
  bool updateProductImage(int product_id, const std::string& image_url, int actor_user_id) const;
  std::optional<ProductRecord> findProductByBarcode(const std::string& barcode) const;
  std::vector<ProductRecord> searchProducts(const std::string& query) const;

  // --- Customers ---
  std::vector<CustomerRecord> listCustomers() const;
  CustomerRecord createCustomer(const std::string& name, const std::string& phone,
                                const std::string& email, int actor_user_id) const;
  bool updateCustomer(int id, const std::string& name, const std::string& phone,
                      const std::string& email, int actor_user_id) const;
  std::optional<CustomerRecord> findCustomerByPhone(const std::string& phone) const;
  bool addLoyaltyPoints(int customer_id, int points) const;

  // --- Discounts ---
  std::vector<DiscountRecord> listDiscounts() const;
  DiscountRecord createDiscount(const std::string& name, const std::string& type,
                                int value, const std::string& promo_code,
                                int min_order_cents, int max_uses,
                                int actor_user_id) const;
  std::optional<DiscountRecord> validatePromoCode(const std::string& code) const;
  bool deactivateDiscount(int id, int actor_user_id) const;

  // --- Shifts ---
  ShiftRecord openShift(int user_id, int opening_cash_cents) const;
  bool closeShift(int shift_id, int user_id, int closing_cash_cents, const std::string& notes) const;
  std::optional<ShiftRecord> getActiveShift(int user_id) const;
  std::optional<ShiftRecord> getShiftById(int shift_id) const;

  // --- Sales ---
  SaleRecord createSaleV2(const SaleCreateInput& input) const;
  std::vector<SaleRecord> listRecentSales(int limit) const;
  std::optional<SaleDetail> getSaleDetail(int sale_id) const;

  // --- Refunds ---
  RefundRecord createRefund(int sale_id, int refunded_by,
                            const std::vector<RefundItemInput>& items,
                            const std::string& reason) const;
  std::vector<RefundRecord> listRefunds(int limit) const;

  // --- Reports ---
  std::vector<SalesReportRow> getSalesReport(const std::string& start_date,
                                             const std::string& end_date) const;
  std::vector<TopProduct> getTopProducts(int limit, const std::string& start_date,
                                         const std::string& end_date) const;

 private:
  std::string connection_string_;
};

}  // namespace pos