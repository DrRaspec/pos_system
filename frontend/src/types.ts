export type UserRole = 'admin' | 'cashier';

export interface User {
  id: number;
  username: string;
  role: UserRole;
}

export interface UserListItem {
  id: number;
  username: string;
  role: UserRole;
  is_active: boolean;
}

export interface Category {
  id: number;
  name: string;
  color: string;
  sort_order: number;
  is_active: boolean;
}

export interface Product {
  id: number;
  sku: string;
  name: string;
  price_cents: number;
  stock_quantity: number;
  image_url: string;
  category_id: number;
  barcode: string;
  tax_rate_percent: number;
  is_active: boolean;
}

export interface Customer {
  id: number;
  name: string;
  phone: string;
  email: string;
  loyalty_points: number;
  is_active: boolean;
}

export interface Discount {
  id: number;
  name: string;
  type: 'percent' | 'fixed';
  value: number;
  promo_code: string;
  min_order_cents: number;
  max_uses: number;
  used_count: number;
  is_active: boolean;
}

export interface Shift {
  id: number;
  opened_by: number;
  closed_by: number;
  opening_cash_cents: number;
  closing_cash_cents: number;
  expected_cash_cents: number;
  notes: string;
  opened_at: string;
  closed_at: string;
}

export interface CartItem {
  product_id: number;
  quantity: number;
}

export interface SalePayment {
  method: 'cash' | 'card' | 'transfer';
  amount_cents: number;
}

export interface Sale {
  id: number;
  receipt_number: string;
  subtotal_cents: number;
  tax_cents: number;
  discount_cents: number;
  total_cents: number;
  payment_method: 'cash' | 'card' | 'transfer' | 'split';
  created_at: string;
  sold_by: string;
  customer_id: number;
}

export interface SaleItemDetail {
  product_id: number;
  product_name: string;
  quantity: number;
  unit_price_cents: number;
  tax_cents: number;
  discount_cents: number;
  line_total_cents: number;
}

export interface SaleDetail {
  sale: Sale;
  items: SaleItemDetail[];
  payments: SalePayment[];
}

export interface Refund {
  id: number;
  sale_id: number;
  receipt_number: string;
  total_cents: number;
  reason: string;
  created_at: string;
}

export interface SalesReportRow {
  date: string;
  sale_count: number;
  revenue_cents: number;
  cash_cents: number;
  card_cents: number;
  transfer_cents: number;
  refund_cents: number;
}

export interface TopProduct {
  product_id: number;
  product_name: string;
  total_quantity: number;
  total_revenue_cents: number;
}

export interface SalesReport {
  report: SalesReportRow[];
  top_products: TopProduct[];
}