import { useCallback, useEffect, useState } from 'react';

import {
  ApiError,
  createProduct,
  createSale,
  createUser,
  fetchMe,
  fetchProducts,
  fetchSales,
  fetchUsers,
  login,
  logout,
  updateProductStock,
  uploadProductImage,
} from './api/client';
import { LoginPage } from './pages/LoginPage';
import { PosPage } from './pages/PosPage';
import { AdminPage } from './pages/AdminPage';
import type { CartItem, Product, Sale, User, UserListItem } from './types';

function getErrorMessage(error: unknown): string {
  if (error instanceof ApiError) return error.message;
  if (error instanceof Error) return error.message;
  return 'Unexpected error.';
}

type AppPage = 'pos' | 'admin';

export default function App() {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const [user, setUser] = useState<User | null>(null);
  const [products, setProducts] = useState<Product[]>([]);
  const [sales, setSales] = useState<Sale[]>([]);
  const [users, setUsers] = useState<UserListItem[]>([]);
  const [bootstrapping, setBootstrapping] = useState(true);
  const [page, setPage] = useState<AppPage>('pos');

  const refreshProducts = useCallback(async () => {
    const nextProducts = await fetchProducts();
    setProducts(nextProducts);
  }, []);

  const refreshSales = useCallback(async () => {
    const nextSales = await fetchSales(50);
    setSales(nextSales);
  }, []);

  const refreshUsers = useCallback(async () => {
    try {
      const nextUsers = await fetchUsers();
      setUsers(nextUsers);
    } catch {
      // non-admin users can't fetch users
    }
  }, []);

  useEffect(() => {
    async function bootstrap() {
      try {
        const currentUser = await fetchMe();
        setUser(currentUser);
        await refreshProducts();
        if (currentUser.role === 'admin') {
          await refreshSales();
          await refreshUsers();
        }
      } catch {
        setUser(null);
      } finally {
        setBootstrapping(false);
      }
    }
    void bootstrap();
  }, [refreshProducts, refreshSales, refreshUsers]);

  async function runAction(action: () => Promise<void>) {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await action();
    } catch (nextError) {
      setError(getErrorMessage(nextError));
    } finally {
      setBusy(false);
    }
  }

  async function handleLogin(username: string, password: string) {
    await runAction(async () => {
      const currentUser = await login(username, password);
      setUser(currentUser);
      await refreshProducts();
      if (currentUser.role === 'admin') {
        await refreshSales();
        await refreshUsers();
      } else {
        setSales([]);
        setUsers([]);
      }
      setPage('pos');
      setNotice('Signed in successfully.');
    });
  }

  async function handleLogout() {
    await runAction(async () => {
      await logout();
      setUser(null);
      setProducts([]);
      setSales([]);
      setUsers([]);
      setPage('pos');
      setNotice('Signed out.');
    });
  }

  async function handleRefreshProducts() {
    await runAction(async () => {
      await refreshProducts();
      setNotice('Products refreshed.');
    });
  }

  async function handleRefreshSales() {
    await runAction(async () => {
      await refreshSales();
      setNotice('Sales refreshed.');
    });
  }

  async function handleRefreshUsers() {
    await runAction(async () => {
      await refreshUsers();
      setNotice('Users refreshed.');
    });
  }

  async function handleCreateProduct(input: {
    sku: string;
    name: string;
    price_cents: number;
    stock_quantity: number;
  }) {
    await runAction(async () => {
      await createProduct(input);
      await refreshProducts();
      setNotice('Product created.');
    });
  }

  async function handleUpdateStock(productId: number, stockQuantity: number) {
    await runAction(async () => {
      await updateProductStock(productId, stockQuantity);
      await refreshProducts();
      setNotice('Stock updated.');
    });
  }

  async function handleUploadImage(productId: number, file: File) {
    await runAction(async () => {
      await uploadProductImage(productId, file);
      await refreshProducts();
      setNotice('Image uploaded.');
    });
  }

  async function handleCreateUser(input: { username: string; password: string; role: 'admin' | 'cashier' }) {
    await runAction(async () => {
      await createUser(input);
      await refreshUsers();
      setNotice('User "' + input.username + '" created as ' + input.role + '.');
    });
  }

  async function handleCheckout(input: {
    payment_method: 'cash' | 'card' | 'transfer';
    items: CartItem[];
  }) {
    await runAction(async () => {
      await createSale(input);
      await refreshProducts();
      if (user?.role === 'admin') {
        await refreshSales();
      }
      setNotice('Sale completed!');
    });
  }

  if (bootstrapping) {
    return (
      <div className="splash-screen">
        <div className="splash-content">
          <div className="brand-icon brand-icon-lg">POS</div>
          <p>Loading secure point-of-sale workspace...</p>
        </div>
      </div>
    );
  }

  if (!user) {
    return <LoginPage loading={busy} error={error} onLogin={handleLogin} />;
  }

  if (page === 'admin' && user.role === 'admin') {
    return (
      <AdminPage
        users={users}
        products={products}
        sales={sales}
        busy={busy}
        error={error}
        notice={notice}
        onCreateUser={handleCreateUser}
        onCreateProduct={handleCreateProduct}
        onUpdateStock={handleUpdateStock}
        onUploadImage={handleUploadImage}
        onRefreshUsers={handleRefreshUsers}
        onRefreshProducts={handleRefreshProducts}
        onRefreshSales={handleRefreshSales}
        onNavigate={setPage}
        onLogout={handleLogout}
        username={user.username}
      />
    );
  }

  return (
    <PosPage
      user={user}
      products={products}
      busy={busy}
      error={error}
      notice={notice}
      onLogout={handleLogout}
      onRefreshProducts={handleRefreshProducts}
      onCheckout={handleCheckout}
      onNavigate={setPage}
    />
  );
}
