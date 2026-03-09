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

export interface Product {
  id: number;
  sku: string;
  name: string;
  price_cents: number;
  stock_quantity: number;
  image_url: string;
  is_active: boolean;
}

export interface CartItem {
  product_id: number;
  quantity: number;
}

export interface Sale {
  id: number;
  total_cents: number;
  payment_method: 'cash' | 'card' | 'transfer';
  created_at: string;
  sold_by: string;
}