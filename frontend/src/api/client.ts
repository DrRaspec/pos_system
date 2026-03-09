import type {
  CartItem, Category, Customer, Discount, Product, Refund, Sale, SaleDetail,
  SalesReport, SalePayment, Shift, User, UserListItem,
} from '../types';

const API_BASE = import.meta.env.VITE_API_BASE_URL ?? '';

class ApiError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    ...init,
    credentials: 'include',
    headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) },
  });
  const raw = await response.text();
  let data: Record<string, unknown> = {};
  if (raw) {
    try { data = JSON.parse(raw) as Record<string, unknown>; } catch { data = {}; }
  }
  if (!response.ok) {
    const message = typeof data.error === 'string' ? data.error : `Request failed with status ${response.status}.`;
    throw new ApiError(message, response.status);
  }
  return data as T;
}

// ---- Auth ----
export async function login(username: string, password: string): Promise<User> {
  const data = await request<{ user: User }>('/api/auth/login', { method: 'POST', body: JSON.stringify({ username, password }) });
  return data.user;
}
export async function logout(): Promise<void> { await request('/api/auth/logout', { method: 'POST', body: '{}' }); }
export async function fetchMe(): Promise<User> { return (await request<{ user: User }>('/api/me')).user; }

// ---- Categories ----
export async function fetchCategories(): Promise<Category[]> { return (await request<{ categories: Category[] }>('/api/categories')).categories; }
export async function createCategory(input: { name: string; color?: string; sort_order?: number }): Promise<Category> {
  return (await request<{ category: Category }>('/api/categories', { method: 'POST', body: JSON.stringify(input) })).category;
}
export async function updateCategory(id: number, input: { name: string; color?: string; sort_order?: number }): Promise<void> {
  await request(`/api/categories/${id}`, { method: 'PATCH', body: JSON.stringify(input) });
}
export async function deleteCategory(id: number): Promise<void> {
  await request(`/api/categories/${id}`, { method: 'DELETE' });
}

// ---- Products ----
export async function fetchProducts(): Promise<Product[]> { return (await request<{ products: Product[] }>('/api/products')).products; }
export async function createProduct(input: { sku: string; name: string; price_cents: number; stock_quantity: number; category_id?: number; barcode?: string; tax_rate_percent?: number }): Promise<Product> {
  return (await request<{ product: Product }>('/api/products', { method: 'POST', body: JSON.stringify(input) })).product;
}
export async function updateProductStock(productId: number, stockQuantity: number): Promise<void> {
  await request(`/api/products/${productId}/stock`, { method: 'PATCH', body: JSON.stringify({ stock_quantity: stockQuantity }) });
}
export async function updateProduct(productId: number, input: { name: string; price_cents: number; stock_quantity: number; category_id?: number; barcode?: string; tax_rate_percent?: number }): Promise<void> {
  await request(`/api/products/${productId}`, { method: 'PATCH', body: JSON.stringify(input) });
}
export async function searchProducts(q: string): Promise<Product[]> {
  return (await request<{ products: Product[] }>(`/api/products/search?q=${encodeURIComponent(q)}`)).products;
}
export async function findProductByBarcode(barcode: string): Promise<Product> {
  return (await request<{ product: Product }>(`/api/products/barcode/${encodeURIComponent(barcode)}`)).product;
}
export async function uploadProductImage(productId: number, file: File): Promise<string> {
  const formData = new FormData();
  formData.append('image', file);
  const response = await fetch(`${API_BASE}/api/products/${productId}/image`, { method: 'POST', credentials: 'include', body: formData });
  const raw = await response.text();
  let data: Record<string, unknown> = {};
  if (raw) { try { data = JSON.parse(raw) as Record<string, unknown>; } catch { data = {}; } }
  if (!response.ok) { throw new ApiError(typeof data.error === 'string' ? data.error : `Upload failed with status ${response.status}.`, response.status); }
  return data.image_url as string;
}

// ---- Customers ----
export async function fetchCustomers(): Promise<Customer[]> { return (await request<{ customers: Customer[] }>('/api/customers')).customers; }
export async function createCustomer(input: { name: string; phone?: string; email?: string }): Promise<Customer> {
  return (await request<{ customer: Customer }>('/api/customers', { method: 'POST', body: JSON.stringify(input) })).customer;
}
export async function updateCustomer(id: number, input: { name: string; phone?: string; email?: string }): Promise<void> {
  await request(`/api/customers/${id}`, { method: 'PATCH', body: JSON.stringify(input) });
}
export async function findCustomerByPhone(phone: string): Promise<Customer | null> {
  try { return (await request<{ customer: Customer }>(`/api/customers/search?phone=${encodeURIComponent(phone)}`)).customer; } catch { return null; }
}

// ---- Discounts ----
export async function fetchDiscounts(): Promise<Discount[]> { return (await request<{ discounts: Discount[] }>('/api/discounts')).discounts; }
export async function createDiscount(input: { name: string; type: 'percent' | 'fixed'; value: number; promo_code?: string; min_order_cents?: number; max_uses?: number }): Promise<Discount> {
  return (await request<{ discount: Discount }>('/api/discounts', { method: 'POST', body: JSON.stringify(input) })).discount;
}
export async function validatePromoCode(code: string): Promise<Discount | null> {
  try { return (await request<{ discount: Discount }>('/api/discounts/validate', { method: 'POST', body: JSON.stringify({ promo_code: code }) })).discount; } catch { return null; }
}
export async function deactivateDiscount(id: number): Promise<void> {
  await request(`/api/discounts/${id}`, { method: 'DELETE' });
}

// ---- Shifts ----
export async function openShift(openingCashCents: number): Promise<Shift> {
  return (await request<{ shift: Shift }>('/api/shifts/open', { method: 'POST', body: JSON.stringify({ opening_cash_cents: openingCashCents }) })).shift;
}
export async function closeShift(shiftId: number, closingCashCents: number, notes?: string): Promise<Shift> {
  return (await request<{ shift: Shift }>('/api/shifts/close', { method: 'POST', body: JSON.stringify({ shift_id: shiftId, closing_cash_cents: closingCashCents, notes: notes ?? '' }) })).shift;
}
export async function fetchActiveShift(): Promise<Shift | null> {
  const data = await request<{ shift: Shift | null }>('/api/shifts/active');
  return data.shift;
}

// ---- Sales ----
export async function createSale(input: {
  payment_method: 'cash' | 'card' | 'transfer' | 'split';
  items: CartItem[];
  customer_id?: number;
  discount_id?: number;
  shift_id?: number;
  payments?: SalePayment[];
}): Promise<Sale> {
  return (await request<{ sale: Sale }>('/api/sales', { method: 'POST', body: JSON.stringify(input) })).sale;
}
export async function fetchSales(limit = 20): Promise<Sale[]> { return (await request<{ sales: Sale[] }>(`/api/sales?limit=${limit}`)).sales; }
export async function fetchSaleDetail(saleId: number): Promise<SaleDetail> {
  return await request<SaleDetail>(`/api/sales/${saleId}`);
}

// ---- Refunds ----
export async function createRefund(input: { sale_id: number; reason: string; items: { product_id: number; quantity: number }[] }): Promise<Refund> {
  return (await request<{ refund: Refund }>('/api/refunds', { method: 'POST', body: JSON.stringify(input) })).refund;
}
export async function fetchRefunds(limit = 20): Promise<Refund[]> { return (await request<{ refunds: Refund[] }>(`/api/refunds?limit=${limit}`)).refunds; }

// ---- Reports ----
export async function fetchSalesReport(start: string, end: string): Promise<SalesReport> {
  return await request<SalesReport>(`/api/reports/sales?start=${start}&end=${end}`);
}

// ---- Users ----
export async function fetchUsers(): Promise<UserListItem[]> { return (await request<{ users: UserListItem[] }>('/api/users')).users; }
export async function createUser(input: { username: string; password: string; role: 'admin' | 'cashier' }): Promise<UserListItem> {
  return (await request<{ user: UserListItem }>('/api/users', { method: 'POST', body: JSON.stringify(input) })).user;
}

export { ApiError };
