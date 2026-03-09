import type { CartItem, Product, Sale, User, UserListItem } from '../types';

const API_BASE = import.meta.env.VITE_API_BASE_URL ?? '';

class ApiError extends Error {
  status: number;

  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

async function request<T>(
  path: string,
  init?: RequestInit,
): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    ...init,
    credentials: 'include',
    headers: {
      'Content-Type': 'application/json',
      ...(init?.headers ?? {}),
    },
  });

  const raw = await response.text();
  let data: Record<string, unknown> = {};
  if (raw) {
    try {
      data = JSON.parse(raw) as Record<string, unknown>;
    } catch {
      data = {};
    }
  }

  if (!response.ok) {
    const message =
      typeof data.error === 'string'
        ? data.error
        : `Request failed with status ${response.status}.`;
    throw new ApiError(message, response.status);
  }

  return data as T;
}

export async function login(username: string, password: string): Promise<User> {
  const data = await request<{ user: User }>('/api/auth/login', {
    method: 'POST',
    body: JSON.stringify({ username, password }),
  });
  return data.user;
}

export async function logout(): Promise<void> {
  await request('/api/auth/logout', {
    method: 'POST',
    body: '{}',
  });
}

export async function fetchMe(): Promise<User> {
  const data = await request<{ user: User }>('/api/me');
  return data.user;
}

export async function fetchProducts(): Promise<Product[]> {
  const data = await request<{ products: Product[] }>('/api/products');
  return data.products;
}

export async function createProduct(input: {
  sku: string;
  name: string;
  price_cents: number;
  stock_quantity: number;
}): Promise<Product> {
  const data = await request<{ product: Product }>('/api/products', {
    method: 'POST',
    body: JSON.stringify(input),
  });
  return data.product;
}

export async function updateProductStock(productId: number, stockQuantity: number): Promise<void> {
  await request(`/api/products/${productId}/stock`, {
    method: 'PATCH',
    body: JSON.stringify({ stock_quantity: stockQuantity }),
  });
}

export async function createSale(input: {
  payment_method: 'cash' | 'card' | 'transfer';
  items: CartItem[];
}): Promise<void> {
  await request('/api/sales', {
    method: 'POST',
    body: JSON.stringify(input),
  });
}

export async function fetchSales(limit = 20): Promise<Sale[]> {
  const data = await request<{ sales: Sale[] }>(`/api/sales?limit=${limit}`);
  return data.sales;
}

export async function fetchUsers(): Promise<UserListItem[]> {
  const data = await request<{ users: UserListItem[] }>('/api/users');
  return data.users;
}

export async function createUser(input: {
  username: string;
  password: string;
  role: 'admin' | 'cashier';
}): Promise<UserListItem> {
  const data = await request<{ user: UserListItem }>('/api/users', {
    method: 'POST',
    body: JSON.stringify(input),
  });
  return data.user;
}

export async function uploadProductImage(productId: number, file: File): Promise<string> {
  const formData = new FormData();
  formData.append('image', file);

  const response = await fetch(`${API_BASE}/api/products/${productId}/image`, {
    method: 'POST',
    credentials: 'include',
    body: formData,
  });

  const raw = await response.text();
  let data: Record<string, unknown> = {};
  if (raw) {
    try {
      data = JSON.parse(raw) as Record<string, unknown>;
    } catch {
      data = {};
    }
  }

  if (!response.ok) {
    const message =
      typeof data.error === 'string'
        ? data.error
        : `Upload failed with status ${response.status}.`;
    throw new ApiError(message, response.status);
  }

  return data.image_url as string;
}

export { ApiError };
