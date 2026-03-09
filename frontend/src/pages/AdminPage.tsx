import { FormEvent, useRef, useState } from 'react';

import type { Product, Sale, UserListItem } from '../types';

interface AdminPageProps {
  users: UserListItem[];
  products: Product[];
  sales: Sale[];
  busy: boolean;
  error: string | null;
  notice: string | null;
  onCreateUser: (input: { username: string; password: string; role: 'admin' | 'cashier' }) => Promise<void>;
  onCreateProduct: (input: { sku: string; name: string; price_cents: number; stock_quantity: number }) => Promise<void>;
  onUpdateProduct: (productId: number, input: { name: string; price_cents: number; stock_quantity: number }) => Promise<void>;
  onUploadImage: (productId: number, file: File) => Promise<void>;
  onRefreshUsers: () => Promise<void>;
  onRefreshProducts: () => Promise<void>;
  onRefreshSales: () => Promise<void>;
  onNavigate: (page: 'pos' | 'admin') => void;
  onLogout: () => Promise<void>;
  username: string;
}

function centsToDisplay(cents: number): string {
  return `$${(cents / 100).toFixed(2)}`;
}

type AdminTab = 'users' | 'products' | 'sales';

export function AdminPage({
  users,
  products,
  sales,
  busy,
  error,
  notice,
  onCreateUser,
  onCreateProduct,
  onUpdateProduct,
  onUploadImage,
  onRefreshUsers,
  onRefreshProducts,
  onRefreshSales,
  onNavigate,
  onLogout,
  username,
}: AdminPageProps) {
  const [tab, setTab] = useState<AdminTab>('users');

  const [newUsername, setNewUsername] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [newRole, setNewRole] = useState<'admin' | 'cashier'>('cashier');

  const [newSku, setNewSku] = useState('');
  const [newName, setNewName] = useState('');
  const [newPriceCents, setNewPriceCents] = useState('');
  const [newStock, setNewStock] = useState('');

  const [editProductId, setEditProductId] = useState('');
  const [editName, setEditName] = useState('');
  const [editPriceCents, setEditPriceCents] = useState('');
  const [editStock, setEditStock] = useState('');

  const [imageProductId, setImageProductId] = useState('');
  const imageInputRef = useRef<HTMLInputElement>(null);

  async function submitCreateUser(e: FormEvent) {
    e.preventDefault();
    await onCreateUser({ username: newUsername.trim(), password: newPassword, role: newRole });
    setNewUsername('');
    setNewPassword('');
    setNewRole('cashier');
  }

  async function submitCreateProduct(e: FormEvent) {
    e.preventDefault();
    await onCreateProduct({
      sku: newSku.trim().toUpperCase(),
      name: newName.trim(),
      price_cents: Number(newPriceCents),
      stock_quantity: Number(newStock),
    });
    setNewSku('');
    setNewName('');
    setNewPriceCents('');
    setNewStock('');
  }

  function handleEditProductSelect(id: string) {
    setEditProductId(id);
    if (id) {
      const p = products.find(pr => pr.id === Number(id));
      if (p) {
        setEditName(p.name);
        setEditPriceCents(String(p.price_cents));
        setEditStock(String(p.stock_quantity));
      }
    } else {
      setEditName('');
      setEditPriceCents('');
      setEditStock('');
    }
  }

  async function submitEditProduct(e: FormEvent) {
    e.preventDefault();
    await onUpdateProduct(Number(editProductId), {
      name: editName.trim(),
      price_cents: Number(editPriceCents),
      stock_quantity: Number(editStock),
    });
    setEditProductId('');
    setEditName('');
    setEditPriceCents('');
    setEditStock('');
  }

  async function submitImageUpload(e: FormEvent) {
    e.preventDefault();
    const file = imageInputRef.current?.files?.[0];
    if (!file || !imageProductId) return;
    await onUploadImage(Number(imageProductId), file);
    setImageProductId('');
    if (imageInputRef.current) imageInputRef.current.value = '';
  }

  return (
    <div className="admin-shell">
      {/* Sidebar */}
      <aside className="admin-sidebar">
        <div className="admin-sidebar-brand">
          <div className="brand-icon">POS</div>
          <span>Admin Panel</span>
        </div>

        <nav className="admin-nav">
          <button
            className={`admin-nav-btn ${tab === 'users' ? 'active' : ''}`}
            onClick={() => { setTab('users'); void onRefreshUsers(); }}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>
            Users
          </button>
          <button
            className={`admin-nav-btn ${tab === 'products' ? 'active' : ''}`}
            onClick={() => { setTab('products'); void onRefreshProducts(); }}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/></svg>
            Products
          </button>
          <button
            className={`admin-nav-btn ${tab === 'sales' ? 'active' : ''}`}
            onClick={() => { setTab('sales'); void onRefreshSales(); }}
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M12 1v22M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"/></svg>
            Sales
          </button>
        </nav>

        <div className="admin-sidebar-footer">
          <button className="admin-nav-btn" onClick={() => onNavigate('pos')}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>
            POS Terminal
          </button>
          <button className="admin-nav-btn logout-btn" onClick={onLogout} disabled={busy}>
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
            Logout
          </button>
          <div className="admin-user-tag">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
            {username}
          </div>
        </div>
      </aside>

      {/* Main content */}
      <main className="admin-main">
        {error && <div className="admin-alert admin-alert-error">{error}</div>}
        {notice && <div className="admin-alert admin-alert-success">{notice}</div>}

        {/* USERS TAB */}
        {tab === 'users' && (
          <div className="admin-content">
            <div className="admin-content-header">
              <h2>User Management</h2>
              <p className="admin-subtitle">Create and manage cashier and admin accounts</p>
            </div>

            <div className="admin-grid-2">
              {/* Create user form */}
              <div className="admin-card">
                <h3>Create New User</h3>
                <form className="admin-form" onSubmit={submitCreateUser}>
                  <div className="form-group">
                    <label htmlFor="new-user">Username</label>
                    <input id="new-user" type="text" placeholder="e.g. sarah_cashier" required minLength={3} maxLength={32}
                      pattern="[A-Za-z0-9_.\-]{3,32}" value={newUsername} onChange={e => setNewUsername(e.target.value)} />
                  </div>
                  <div className="form-group">
                    <label htmlFor="new-pass">Password</label>
                    <input id="new-pass" type="password" placeholder="Min 8 characters" required minLength={8} maxLength={256}
                      value={newPassword} onChange={e => setNewPassword(e.target.value)} />
                  </div>
                  <div className="form-group">
                    <label htmlFor="new-role">Role</label>
                    <select id="new-role" value={newRole} onChange={e => setNewRole(e.target.value as 'admin' | 'cashier')}>
                      <option value="cashier">Cashier</option>
                      <option value="admin">Admin</option>
                    </select>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>
                    {busy ? 'Creating...' : 'Create User'}
                  </button>
                </form>
              </div>

              {/* User list */}
              <div className="admin-card">
                <div className="admin-card-header">
                  <h3>All Users</h3>
                  <span className="badge">{users.length}</span>
                </div>
                <div className="admin-table-wrap">
                  <table className="admin-table">
                    <thead>
                      <tr>
                        <th>ID</th>
                        <th>Username</th>
                        <th>Role</th>
                        <th>Status</th>
                      </tr>
                    </thead>
                    <tbody>
                      {users.map(u => (
                        <tr key={u.id}>
                          <td>{u.id}</td>
                          <td className="font-medium">{u.username}</td>
                          <td>
                            <span className={`role-badge role-${u.role}`}>{u.role}</span>
                          </td>
                          <td>
                            <span className={`status-dot ${u.is_active ? 'active' : 'inactive'}`} />
                            {u.is_active ? 'Active' : 'Inactive'}
                          </td>
                        </tr>
                      ))}
                      {users.length === 0 && (
                        <tr><td colSpan={4} className="text-center text-muted">No users found</td></tr>
                      )}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* PRODUCTS TAB */}
        {tab === 'products' && (
          <div className="admin-content">
            <div className="admin-content-header">
              <h2>Product Management</h2>
              <p className="admin-subtitle">Add products, update stock, and upload images</p>
            </div>

            <div className="admin-grid-3">
              {/* Create product */}
              <div className="admin-card">
                <h3>Add Product</h3>
                <form className="admin-form" onSubmit={submitCreateProduct}>
                  <div className="form-group">
                    <label htmlFor="prod-sku">SKU</label>
                    <input id="prod-sku" type="text" placeholder="e.g. COFFEE-LG" required minLength={3} maxLength={32}
                      value={newSku} onChange={e => setNewSku(e.target.value)} />
                  </div>
                  <div className="form-group">
                    <label htmlFor="prod-name">Name</label>
                    <input id="prod-name" type="text" placeholder="e.g. Large Coffee" required maxLength={120}
                      value={newName} onChange={e => setNewName(e.target.value)} />
                  </div>
                  <div className="form-row">
                    <div className="form-group">
                      <label htmlFor="prod-price">Price (cents)</label>
                      <input id="prod-price" type="number" min={1} required placeholder="500"
                        value={newPriceCents} onChange={e => setNewPriceCents(e.target.value)} />
                    </div>
                    <div className="form-group">
                      <label htmlFor="prod-stock">Stock</label>
                      <input id="prod-stock" type="number" min={0} required placeholder="100"
                        value={newStock} onChange={e => setNewStock(e.target.value)} />
                    </div>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Add Product</button>
                </form>
              </div>

              {/* Edit product */}
              <div className="admin-card">
                <h3>Edit Product</h3>
                <form className="admin-form" onSubmit={submitEditProduct}>
                  <div className="form-group">
                    <label htmlFor="edit-pid">Product</label>
                    <select id="edit-pid" required value={editProductId} onChange={e => handleEditProductSelect(e.target.value)}>
                      <option value="">Select product...</option>
                      {products.map(p => (
                        <option key={p.id} value={p.id}>{p.name}</option>
                      ))}
                    </select>
                  </div>
                  <div className="form-group">
                    <label htmlFor="edit-name">Name</label>
                    <input id="edit-name" type="text" required maxLength={120} placeholder="Product name"
                      value={editName} onChange={e => setEditName(e.target.value)} />
                  </div>
                  <div className="form-row">
                    <div className="form-group">
                      <label htmlFor="edit-price">Price (cents)</label>
                      <input id="edit-price" type="number" min={1} required placeholder="500"
                        value={editPriceCents} onChange={e => setEditPriceCents(e.target.value)} />
                    </div>
                    <div className="form-group">
                      <label htmlFor="edit-stock">Stock</label>
                      <input id="edit-stock" type="number" min={0} required placeholder="0"
                        value={editStock} onChange={e => setEditStock(e.target.value)} />
                    </div>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy || !editProductId}>Update Product</button>
                </form>
              </div>

              {/* Upload image */}
              <div className="admin-card">
                <h3>Upload Image</h3>
                <form className="admin-form" onSubmit={submitImageUpload}>
                  <div className="form-group">
                    <label htmlFor="img-pid">Product</label>
                    <select id="img-pid" required value={imageProductId} onChange={e => setImageProductId(e.target.value)}>
                      <option value="">Select product...</option>
                      {products.map(p => (
                        <option key={p.id} value={p.id}>{p.name}</option>
                      ))}
                    </select>
                  </div>
                  <div className="form-group">
                    <label htmlFor="img-file">Image (JPEG, PNG, WebP, max 2MB)</label>
                    <input id="img-file" type="file" accept="image/jpeg,image/png,image/webp" required ref={imageInputRef} />
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Upload</button>
                </form>
              </div>
            </div>

            {/* Product inventory table */}
            <div className="admin-card" style={{ marginTop: '1.5rem' }}>
              <div className="admin-card-header">
                <h3>Inventory</h3>
                <span className="badge">{products.length} items</span>
              </div>
              <div className="admin-table-wrap">
                <table className="admin-table">
                  <thead>
                    <tr>
                      <th>Image</th>
                      <th>SKU</th>
                      <th>Name</th>
                      <th>Price</th>
                      <th>Stock</th>
                    </tr>
                  </thead>
                  <tbody>
                    {products.map(p => (
                      <tr key={p.id}>
                        <td>
                          {p.image_url ? (
                            <img src={p.image_url} alt={p.name} className="product-thumb" />
                          ) : (
                            <div className="product-thumb-placeholder">
                              <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
                            </div>
                          )}
                        </td>
                        <td><code>{p.sku}</code></td>
                        <td className="font-medium">{p.name}</td>
                        <td>{centsToDisplay(p.price_cents)}</td>
                        <td>
                          <span className={p.stock_quantity > 0 ? 'text-ok' : 'text-warn'}>
                            {p.stock_quantity}
                          </span>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        )}

        {/* SALES TAB */}
        {tab === 'sales' && (
          <div className="admin-content">
            <div className="admin-content-header">
              <h2>Sales History</h2>
              <p className="admin-subtitle">Recent transactions</p>
            </div>

            <div className="admin-card">
              <div className="admin-table-wrap">
                <table className="admin-table">
                  <thead>
                    <tr>
                      <th>ID</th>
                      <th>Date</th>
                      <th>Cashier</th>
                      <th>Method</th>
                      <th>Total</th>
                    </tr>
                  </thead>
                  <tbody>
                    {sales.map(s => (
                      <tr key={s.id}>
                        <td>#{s.id}</td>
                        <td>{new Date(s.created_at).toLocaleString()}</td>
                        <td>{s.sold_by}</td>
                        <td>
                          <span className={`payment-badge payment-${s.payment_method}`}>
                            {s.payment_method}
                          </span>
                        </td>
                        <td className="font-medium">{centsToDisplay(s.total_cents)}</td>
                      </tr>
                    ))}
                    {sales.length === 0 && (
                      <tr><td colSpan={5} className="text-center text-muted">No sales yet</td></tr>
                    )}
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        )}
      </main>
    </div>
  );
}
