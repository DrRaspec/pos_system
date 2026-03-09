import { useCallback, useEffect, useState } from 'react';

import * as api from './api/client';
import { LoginPage } from './pages/LoginPage';
import { PosPage } from './pages/PosPage';
import { AdminPage } from './pages/AdminPage';
import type { Category, Customer, Discount, Product, Refund, Sale, SalesReport, Shift, User, UserListItem } from './types';

type Page = 'login' | 'pos' | 'admin';

export default function App() {
  const [page, setPage] = useState<Page>('login');
  const [user, setUser] = useState<User | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  // Data
  const [products, setProducts] = useState<Product[]>([]);
  const [categories, setCategories] = useState<Category[]>([]);
  const [customers, setCustomers] = useState<Customer[]>([]);
  const [discounts, setDiscounts] = useState<Discount[]>([]);
  const [sales, setSales] = useState<Sale[]>([]);
  const [refunds, setRefunds] = useState<Refund[]>([]);
  const [users, setUsers] = useState<UserListItem[]>([]);
  const [shift, setShift] = useState<Shift | null>(null);
  const [salesReport, setSalesReport] = useState<SalesReport | null>(null);

  function flash(msg: string) { setNotice(msg); setTimeout(() => setNotice(null), 3000); }
  function showError(msg: string) { setError(msg); setTimeout(() => setError(null), 5000); }

  const refreshProducts = useCallback(async () => { try { setProducts(await api.fetchProducts()); } catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } }, []);
  const refreshCategories = useCallback(async () => { try { setCategories(await api.fetchCategories()); } catch { /* silent */ } }, []);
  const refreshCustomers = useCallback(async () => { try { setCustomers(await api.fetchCustomers()); } catch { /* silent */ } }, []);
  const refreshDiscounts = useCallback(async () => { try { setDiscounts(await api.fetchDiscounts()); } catch { /* silent – non-admin */ } }, []);
  const refreshSales = useCallback(async () => { try { setSales(await api.fetchSales(50)); } catch { /* silent */ } }, []);
  const refreshRefunds = useCallback(async () => { try { setRefunds(await api.fetchRefunds(50)); } catch { /* silent */ } }, []);
  const refreshUsers = useCallback(async () => { try { setUsers(await api.fetchUsers()); } catch { /* silent */ } }, []);
  const refreshShift = useCallback(async () => { try { setShift(await api.fetchActiveShift()); } catch { /* silent */ } }, []);

  async function loadAllData() {
    await Promise.allSettled([refreshProducts(), refreshCategories(), refreshCustomers(), refreshShift()]);
  }

  async function loadAdminData() {
    await Promise.allSettled([refreshUsers(), refreshDiscounts(), refreshSales(), refreshRefunds()]);
  }

  // Check session on mount
  useEffect(() => {
    (async () => {
      try {
        const me = await api.fetchMe();
        setUser(me);
        setPage('pos');
        await loadAllData();
        if (me.role === 'admin') await loadAdminData();
      } catch { /* not logged in */ }
    })();
  }, []);

  // ---- Auth ----
  async function handleLogin(username: string, password: string) {
    setError(null);
    setBusy(true);
    try {
      const u = await api.login(username, password);
      setUser(u);
      setPage('pos');
      await loadAllData();
      if (u.role === 'admin') await loadAdminData();
    } catch (e: unknown) {
      showError(e instanceof Error ? e.message : 'Login failed.');
    } finally { setBusy(false); }
  }

  async function handleLogout() {
    try { await api.logout(); } catch { /* silent */ }
    setUser(null);
    setPage('login');
    setProducts([]); setCategories([]); setCustomers([]); setDiscounts([]); setSales([]); setRefunds([]); setUsers([]); setShift(null); setSalesReport(null);
  }

  // ---- Navigation ----
  function navigateTo(p: 'pos' | 'admin') {
    if (!user) return;
    if (p === 'admin' && user.role !== 'admin') return;
    setPage(p);
    setError(null);
    if (p === 'admin') loadAdminData();
  }

  // ---- Checkout ----
  async function handleCheckout(input: Parameters<typeof api.createSale>[0]) {
    setError(null);
    setBusy(true);
    try {
      const sale = await api.createSale(input);
      flash(`Sale ${sale.receipt_number} complete!`);
      await refreshProducts();
      await refreshShift();
      return sale;
    } catch (e: unknown) {
      showError(e instanceof Error ? e.message : 'Checkout failed.');
    } finally { setBusy(false); }
  }

  // ---- Shift ----
  async function handleOpenShift(openingCash: number) {
    setBusy(true);
    try {
      const s = await api.openShift(openingCash);
      setShift(s);
      flash('Shift opened!');
    } catch (e: unknown) {
      showError(e instanceof Error ? e.message : 'Failed to open shift.');
    } finally { setBusy(false); }
  }

  async function handleCloseShift(shiftId: number, closingCash: number, notes: string) {
    setBusy(true);
    try {
      await api.closeShift(shiftId, closingCash, notes);
      setShift(null);
      flash('Shift closed!');
    } catch (e: unknown) {
      showError(e instanceof Error ? e.message : 'Failed to close shift.');
    } finally { setBusy(false); }
  }

  // ---- Promo / Customer ----
  async function handleValidatePromo(code: string) {
    try { return await api.validatePromoCode(code) ?? null; } catch { showError('Invalid promo code.'); return null; }
  }

  async function handleFindCustomerByPhone(phone: string) {
    try { return await api.findCustomerByPhone(phone); } catch { return null; }
  }

  async function handleCreateCustomer(input: { name: string; phone?: string }) {
    setBusy(true);
    try {
      await api.createCustomer(input);
      await refreshCustomers();
      flash('Customer created!');
    } catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleFindByBarcode(barcode: string) {
    try { return await api.findProductByBarcode(barcode); } catch { showError('Product not found.'); return null; }
  }

  // ---- Admin Actions ----
  async function handleCreateUser(input: { username: string; password: string; role: 'admin' | 'cashier' }) {
    setBusy(true);
    try { await api.createUser(input); await refreshUsers(); flash('User created!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleCreateProduct(input: Parameters<typeof api.createProduct>[0]) {
    setBusy(true);
    try { await api.createProduct(input); await refreshProducts(); flash('Product created!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleUpdateProduct(productId: number, input: Parameters<typeof api.updateProduct>[1]) {
    setBusy(true);
    try { await api.updateProduct(productId, input); await refreshProducts(); flash('Product updated!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleUploadImage(productId: number, file: File) {
    setBusy(true);
    try { await api.uploadProductImage(productId, file); await refreshProducts(); flash('Image uploaded!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleCreateCategory(input: { name: string; color?: string; sort_order?: number }) {
    setBusy(true);
    try { await api.createCategory(input); await refreshCategories(); flash('Category created!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleDeleteCategory(id: number) {
    setBusy(true);
    try { await api.deleteCategory(id); await refreshCategories(); flash('Category deleted.'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleCreateDiscount(input: Parameters<typeof api.createDiscount>[0]) {
    setBusy(true);
    try { await api.createDiscount(input); await refreshDiscounts(); flash('Discount created!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleDeactivateDiscount(id: number) {
    setBusy(true);
    try { await api.deactivateDiscount(id); await refreshDiscounts(); flash('Discount deactivated.'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleCreateRefund(input: Parameters<typeof api.createRefund>[0]) {
    setBusy(true);
    try { await api.createRefund(input); await refreshRefunds(); await refreshProducts(); flash('Refund processed!'); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  async function handleFetchReport(start: string, end: string) {
    setBusy(true);
    try { setSalesReport(await api.fetchSalesReport(start, end)); }
    catch (e: unknown) { showError(e instanceof Error ? e.message : 'Failed.'); } finally { setBusy(false); }
  }

  if (page === 'login' || !user) {
    return <LoginPage loading={busy} error={error} onLogin={handleLogin} />;
  }

  if (page === 'admin' && user.role === 'admin') {
    return (
      <AdminPage
        users={users} products={products} categories={categories} customers={customers}
        discounts={discounts} sales={sales} refunds={refunds} salesReport={salesReport}
        busy={busy} error={error} notice={notice} username={user.username}
        onCreateUser={handleCreateUser} onCreateProduct={handleCreateProduct}
        onUpdateProduct={handleUpdateProduct} onUploadImage={handleUploadImage}
        onCreateCategory={handleCreateCategory} onDeleteCategory={handleDeleteCategory}
        onCreateCustomer={handleCreateCustomer} onCreateDiscount={handleCreateDiscount}
        onDeactivateDiscount={handleDeactivateDiscount} onCreateRefund={handleCreateRefund}
        onFetchReport={handleFetchReport} onRefreshUsers={refreshUsers}
        onRefreshProducts={refreshProducts} onRefreshSales={refreshSales}
        onNavigate={navigateTo} onLogout={handleLogout}
      />
    );
  }

  return (
    <PosPage
      user={user} products={products} categories={categories} customers={customers}
      shift={shift} busy={busy} error={error} notice={notice}
      onLogout={handleLogout} onRefreshProducts={refreshProducts}
      onCheckout={handleCheckout} onNavigate={navigateTo}
      onOpenShift={handleOpenShift} onCloseShift={handleCloseShift}
      onValidatePromo={handleValidatePromo} onFindCustomerByPhone={handleFindCustomerByPhone}
      onCreateCustomer={handleCreateCustomer} onFindByBarcode={handleFindByBarcode}
    />
  );
}
