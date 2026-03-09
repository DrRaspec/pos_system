import { FormEvent, useRef, useState } from 'react';

import type { Category, Customer, Discount, Product, Refund, Sale, SalesReport, UserListItem } from '../types';

interface AdminPageProps {
  users: UserListItem[];
  products: Product[];
  categories: Category[];
  customers: Customer[];
  discounts: Discount[];
  sales: Sale[];
  refunds: Refund[];
  salesReport: SalesReport | null;
  busy: boolean;
  error: string | null;
  notice: string | null;
  onCreateUser: (input: { username: string; password: string; role: 'admin' | 'cashier' }) => Promise<void>;
  onCreateProduct: (input: { sku: string; name: string; price_cents: number; stock_quantity: number; category_id?: number; barcode?: string; tax_rate_percent?: number }) => Promise<void>;
  onUpdateProduct: (productId: number, input: { name: string; price_cents: number; stock_quantity: number; category_id?: number; barcode?: string; tax_rate_percent?: number }) => Promise<void>;
  onUploadImage: (productId: number, file: File) => Promise<void>;
  onCreateCategory: (input: { name: string; color?: string; sort_order?: number }) => Promise<void>;
  onDeleteCategory: (id: number) => Promise<void>;
  onCreateCustomer: (input: { name: string; phone?: string; email?: string }) => Promise<void>;
  onCreateDiscount: (input: { name: string; type: 'percent' | 'fixed'; value: number; promo_code?: string; min_order_cents?: number; max_uses?: number }) => Promise<void>;
  onDeactivateDiscount: (id: number) => Promise<void>;
  onCreateRefund: (input: { sale_id: number; reason: string; items: { product_id: number; quantity: number }[] }) => Promise<void>;
  onFetchReport: (start: string, end: string) => Promise<void>;
  onRefreshUsers: () => Promise<void>;
  onRefreshProducts: () => Promise<void>;
  onRefreshSales: () => Promise<void>;
  onNavigate: (page: 'pos' | 'admin') => void;
  onLogout: () => Promise<void>;
  username: string;
}

function centsToDisplay(cents: number): string {
  return '$' + (cents / 100).toFixed(2);
}

type AdminTab = 'products' | 'categories' | 'customers' | 'discounts' | 'sales' | 'refunds' | 'reports' | 'users';

export function AdminPage({
  users, products, categories, customers, discounts, sales, refunds, salesReport,
  busy, error, notice,
  onCreateUser, onCreateProduct, onUpdateProduct, onUploadImage,
  onCreateCategory, onDeleteCategory,
  onCreateCustomer, onCreateDiscount, onDeactivateDiscount,
  onCreateRefund, onFetchReport,
  onRefreshUsers, onRefreshProducts, onRefreshSales,
  onNavigate, onLogout, username,
}: AdminPageProps) {
  const [tab, setTab] = useState<AdminTab>('products');

  // User form
  const [newUsername, setNewUsername] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [newRole, setNewRole] = useState<'admin' | 'cashier'>('cashier');

  // Product form
  const [newSku, setNewSku] = useState('');
  const [newName, setNewName] = useState('');
  const [newPriceCents, setNewPriceCents] = useState('');
  const [newStock, setNewStock] = useState('');
  const [newCategoryId, setNewCategoryId] = useState('0');
  const [newBarcode, setNewBarcode] = useState('');
  const [newTaxRate, setNewTaxRate] = useState('0');

  // Edit product
  const [editProductId, setEditProductId] = useState('');
  const [editName, setEditName] = useState('');
  const [editPriceCents, setEditPriceCents] = useState('');
  const [editStock, setEditStock] = useState('');
  const [editCategoryId, setEditCategoryId] = useState('0');
  const [editBarcode, setEditBarcode] = useState('');
  const [editTaxRate, setEditTaxRate] = useState('0');

  // Image
  const [imageProductId, setImageProductId] = useState('');
  const imageInputRef = useRef<HTMLInputElement>(null);

  // Category
  const [catName, setCatName] = useState('');
  const [catColor, setCatColor] = useState('#6366f1');

  // Customer
  const [custName, setCustName] = useState('');
  const [custPhone, setCustPhone] = useState('');
  const [custEmail, setCustEmail] = useState('');

  // Discount
  const [discName, setDiscName] = useState('');
  const [discType, setDiscType] = useState<'percent' | 'fixed'>('percent');
  const [discValue, setDiscValue] = useState('');
  const [discPromo, setDiscPromo] = useState('');
  const [discMinOrder, setDiscMinOrder] = useState('');
  const [discMaxUses, setDiscMaxUses] = useState('');

  // Refund
  const [refundSaleId, setRefundSaleId] = useState('');
  const [refundReason, setRefundReason] = useState('');
  const [refundItems, setRefundItems] = useState<{ product_id: string; quantity: string }[]>([{ product_id: '', quantity: '1' }]);

  // Report
  const [reportStart, setReportStart] = useState(() => { const d = new Date(); d.setDate(d.getDate() - 7); return d.toISOString().slice(0, 10); });
  const [reportEnd, setReportEnd] = useState(() => new Date().toISOString().slice(0, 10));

  async function submitCreateUser(e: FormEvent) {
    e.preventDefault();
    await onCreateUser({ username: newUsername, password: newPassword, role: newRole });
    setNewUsername(''); setNewPassword('');
  }

  async function submitCreateProduct(e: FormEvent) {
    e.preventDefault();
    await onCreateProduct({
      sku: newSku, name: newName, price_cents: Number(newPriceCents), stock_quantity: Number(newStock),
      category_id: Number(newCategoryId) || undefined, barcode: newBarcode || undefined,
      tax_rate_percent: Number(newTaxRate) || undefined,
    });
    setNewSku(''); setNewName(''); setNewPriceCents(''); setNewStock(''); setNewCategoryId('0'); setNewBarcode(''); setNewTaxRate('0');
  }

  function handleEditProductSelect(id: string) {
    setEditProductId(id);
    if (id) {
      const p = products.find(pr => pr.id === Number(id));
      if (p) {
        setEditName(p.name); setEditPriceCents(String(p.price_cents)); setEditStock(String(p.stock_quantity));
        setEditCategoryId(String(p.category_id)); setEditBarcode(p.barcode); setEditTaxRate(String(p.tax_rate_percent));
      }
    } else {
      setEditName(''); setEditPriceCents(''); setEditStock(''); setEditCategoryId('0'); setEditBarcode(''); setEditTaxRate('0');
    }
  }

  async function submitEditProduct(e: FormEvent) {
    e.preventDefault();
    await onUpdateProduct(Number(editProductId), {
      name: editName.trim(), price_cents: Number(editPriceCents), stock_quantity: Number(editStock),
      category_id: Number(editCategoryId) || undefined, barcode: editBarcode || undefined,
      tax_rate_percent: Number(editTaxRate) || undefined,
    });
    setEditProductId(''); setEditName(''); setEditPriceCents(''); setEditStock('');
    setEditCategoryId('0'); setEditBarcode(''); setEditTaxRate('0');
  }

  async function submitImageUpload(e: FormEvent) {
    e.preventDefault();
    const file = imageInputRef.current?.files?.[0];
    if (!file) return;
    await onUploadImage(Number(imageProductId), file);
    setImageProductId(''); if (imageInputRef.current) imageInputRef.current.value = '';
  }

  async function submitCreateCategory(e: FormEvent) {
    e.preventDefault();
    await onCreateCategory({ name: catName, color: catColor });
    setCatName(''); setCatColor('#6366f1');
  }

  async function submitCreateCustomer(e: FormEvent) {
    e.preventDefault();
    await onCreateCustomer({ name: custName, phone: custPhone || undefined, email: custEmail || undefined });
    setCustName(''); setCustPhone(''); setCustEmail('');
  }

  async function submitCreateDiscount(e: FormEvent) {
    e.preventDefault();
    await onCreateDiscount({
      name: discName, type: discType, value: Number(discValue),
      promo_code: discPromo || undefined, min_order_cents: Number(discMinOrder) || undefined,
      max_uses: Number(discMaxUses) || undefined,
    });
    setDiscName(''); setDiscValue(''); setDiscPromo(''); setDiscMinOrder(''); setDiscMaxUses('');
  }

  async function submitRefund(e: FormEvent) {
    e.preventDefault();
    const items = refundItems.filter(i => i.product_id).map(i => ({ product_id: Number(i.product_id), quantity: Number(i.quantity) }));
    if (items.length === 0) return;
    await onCreateRefund({ sale_id: Number(refundSaleId), reason: refundReason, items });
    setRefundSaleId(''); setRefundReason(''); setRefundItems([{ product_id: '', quantity: '1' }]);
  }

  async function handleFetchReport(e: FormEvent) {
    e.preventDefault();
    await onFetchReport(reportStart, reportEnd);
  }

  const tabs: { key: AdminTab; label: string }[] = [
    { key: 'products', label: 'Products' },
    { key: 'categories', label: 'Categories' },
    { key: 'customers', label: 'Customers' },
    { key: 'discounts', label: 'Discounts' },
    { key: 'sales', label: 'Sales' },
    { key: 'refunds', label: 'Refunds' },
    { key: 'reports', label: 'Reports' },
    { key: 'users', label: 'Users' },
  ];

  // Report totals
  const reportTotals = salesReport ? {
    totalSales: salesReport.report.reduce((s, r) => s + r.sale_count, 0),
    totalRevenue: salesReport.report.reduce((s, r) => s + r.revenue_cents, 0),
    totalCash: salesReport.report.reduce((s, r) => s + r.cash_cents, 0),
    totalCard: salesReport.report.reduce((s, r) => s + r.card_cents, 0),
    totalTransfer: salesReport.report.reduce((s, r) => s + r.transfer_cents, 0),
  } : null;

  const maxRevenue = salesReport ? Math.max(...salesReport.report.map(r => r.revenue_cents), 1) : 1;

  return (
    <div className="admin-layout">
      <header className="admin-header">
        <div className="header-left">
          <div className="brand-icon">POS</div>
          <span className="header-title">Admin Panel</span>
        </div>
        <div className="header-right">
          <span className="user-label">{username}</span>
          <button className="btn btn-ghost" onClick={() => onNavigate('pos')}>POS</button>
          <button className="btn btn-ghost" onClick={onLogout}>Logout</button>
        </div>
      </header>

      {error && <div className="alert alert-error">{error}</div>}
      {notice && <div className="alert alert-success">{notice}</div>}

      <div className="admin-body">
        <nav className="admin-tabs">
          {tabs.map(t => (
            <button key={t.key} className={`admin-tab ${tab === t.key ? 'active' : ''}`} onClick={() => setTab(t.key)}>{t.label}</button>
          ))}
        </nav>

        <div className="admin-content">
          {/* ============ PRODUCTS ============ */}
          {tab === 'products' && (
            <div className="admin-section">
              <div className="admin-grid">
                <div className="admin-card">
                  <h3>Create Product</h3>
                  <form className="admin-form" onSubmit={submitCreateProduct}>
                    <div className="form-row">
                      <div className="form-group"><label>SKU</label><input required value={newSku} onChange={e => setNewSku(e.target.value)} placeholder="PROD-001" /></div>
                      <div className="form-group"><label>Name</label><input required value={newName} onChange={e => setNewName(e.target.value)} placeholder="Product" /></div>
                    </div>
                    <div className="form-row">
                      <div className="form-group"><label>Price (cents)</label><input type="number" min={1} required value={newPriceCents} onChange={e => setNewPriceCents(e.target.value)} /></div>
                      <div className="form-group"><label>Stock</label><input type="number" min={0} required value={newStock} onChange={e => setNewStock(e.target.value)} /></div>
                    </div>
                    <div className="form-row">
                      <div className="form-group"><label>Category</label>
                        <select value={newCategoryId} onChange={e => setNewCategoryId(e.target.value)}>
                          <option value="0">None</option>
                          {categories.map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
                        </select>
                      </div>
                      <div className="form-group"><label>Barcode</label><input value={newBarcode} onChange={e => setNewBarcode(e.target.value)} placeholder="Optional" /></div>
                    </div>
                    <div className="form-group"><label>Tax Rate (%)</label><input type="number" step="0.01" min={0} max={100} value={newTaxRate} onChange={e => setNewTaxRate(e.target.value)} /></div>
                    <button type="submit" className="btn btn-primary" disabled={busy}>Create Product</button>
                  </form>
                </div>

                <div className="admin-card">
                  <h3>Edit Product</h3>
                  <form className="admin-form" onSubmit={submitEditProduct}>
                    <div className="form-group"><label>Product</label>
                      <select required value={editProductId} onChange={e => handleEditProductSelect(e.target.value)}>
                        <option value="">Select...</option>
                        {products.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}
                      </select>
                    </div>
                    {editProductId && (
                      <>
                        <div className="form-group"><label>Name</label><input required value={editName} onChange={e => setEditName(e.target.value)} /></div>
                        <div className="form-row">
                          <div className="form-group"><label>Price (cents)</label><input type="number" min={1} required value={editPriceCents} onChange={e => setEditPriceCents(e.target.value)} /></div>
                          <div className="form-group"><label>Stock</label><input type="number" min={0} required value={editStock} onChange={e => setEditStock(e.target.value)} /></div>
                        </div>
                        <div className="form-row">
                          <div className="form-group"><label>Category</label>
                            <select value={editCategoryId} onChange={e => setEditCategoryId(e.target.value)}>
                              <option value="0">None</option>
                              {categories.map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
                            </select>
                          </div>
                          <div className="form-group"><label>Barcode</label><input value={editBarcode} onChange={e => setEditBarcode(e.target.value)} /></div>
                        </div>
                        <div className="form-group"><label>Tax Rate (%)</label><input type="number" step="0.01" min={0} max={100} value={editTaxRate} onChange={e => setEditTaxRate(e.target.value)} /></div>
                        <button type="submit" className="btn btn-primary" disabled={busy}>Update</button>
                      </>
                    )}
                  </form>
                </div>

                <div className="admin-card">
                  <h3>Upload Image</h3>
                  <form className="admin-form" onSubmit={submitImageUpload}>
                    <div className="form-group"><label>Product</label>
                      <select required value={imageProductId} onChange={e => setImageProductId(e.target.value)}>
                        <option value="">Select...</option>
                        {products.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}
                      </select>
                    </div>
                    <div className="form-group"><label>Image</label><input type="file" accept="image/jpeg,image/png,image/webp" ref={imageInputRef} required /></div>
                    <button type="submit" className="btn btn-primary" disabled={busy}>Upload</button>
                  </form>
                </div>
              </div>

              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <div className="card-header"><h3>Products ({products.length})</h3><button className="btn btn-ghost btn-sm" onClick={onRefreshProducts}>↻</button></div>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>SKU</th><th>Name</th><th>Price</th><th>Stock</th><th>Category</th><th>Tax</th><th>Barcode</th></tr></thead>
                    <tbody>
                      {products.map(p => (
                        <tr key={p.id}><td>{p.id}</td><td>{p.sku}</td><td>{p.name}</td><td>{centsToDisplay(p.price_cents)}</td><td>{p.stock_quantity}</td>
                          <td>{categories.find(c => c.id === p.category_id)?.name ?? '—'}</td><td>{p.tax_rate_percent}%</td><td>{p.barcode || '—'}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ CATEGORIES ============ */}
          {tab === 'categories' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Create Category</h3>
                <form className="admin-form inline-form" onSubmit={submitCreateCategory}>
                  <input required placeholder="Category name" value={catName} onChange={e => setCatName(e.target.value)} />
                  <input type="color" value={catColor} onChange={e => setCatColor(e.target.value)} title="Color" />
                  <button type="submit" className="btn btn-primary" disabled={busy}>Add</button>
                </form>
              </div>
              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <h3>Categories ({categories.length})</h3>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Name</th><th>Color</th><th>Actions</th></tr></thead>
                    <tbody>
                      {categories.map(c => (
                        <tr key={c.id}><td>{c.id}</td><td>{c.name}</td><td><span className="color-dot" style={{ background: c.color }} />{c.color}</td>
                          <td><button className="btn btn-xs btn-danger" onClick={() => onDeleteCategory(c.id)}>Delete</button></td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ CUSTOMERS ============ */}
          {tab === 'customers' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Add Customer</h3>
                <form className="admin-form" onSubmit={submitCreateCustomer}>
                  <div className="form-row">
                    <div className="form-group"><label>Name</label><input required value={custName} onChange={e => setCustName(e.target.value)} /></div>
                    <div className="form-group"><label>Phone</label><input value={custPhone} onChange={e => setCustPhone(e.target.value)} /></div>
                    <div className="form-group"><label>Email</label><input type="email" value={custEmail} onChange={e => setCustEmail(e.target.value)} /></div>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Add Customer</button>
                </form>
              </div>
              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <h3>Customers ({customers.length})</h3>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Name</th><th>Phone</th><th>Email</th><th>Loyalty</th></tr></thead>
                    <tbody>
                      {customers.map(c => (
                        <tr key={c.id}><td>{c.id}</td><td>{c.name}</td><td>{c.phone || '—'}</td><td>{c.email || '—'}</td><td>{c.loyalty_points} pts</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ DISCOUNTS ============ */}
          {tab === 'discounts' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Create Discount</h3>
                <form className="admin-form" onSubmit={submitCreateDiscount}>
                  <div className="form-row">
                    <div className="form-group"><label>Name</label><input required value={discName} onChange={e => setDiscName(e.target.value)} placeholder="Summer Sale" /></div>
                    <div className="form-group"><label>Type</label>
                      <select value={discType} onChange={e => setDiscType(e.target.value as 'percent' | 'fixed')}>
                        <option value="percent">Percentage</option>
                        <option value="fixed">Fixed Amount</option>
                      </select>
                    </div>
                    <div className="form-group"><label>Value {discType === 'percent' ? '(%)' : '(cents)'}</label><input type="number" min={1} required value={discValue} onChange={e => setDiscValue(e.target.value)} /></div>
                  </div>
                  <div className="form-row">
                    <div className="form-group"><label>Promo Code</label><input value={discPromo} onChange={e => setDiscPromo(e.target.value)} placeholder="SAVE10" /></div>
                    <div className="form-group"><label>Min Order (cents)</label><input type="number" min={0} value={discMinOrder} onChange={e => setDiscMinOrder(e.target.value)} /></div>
                    <div className="form-group"><label>Max Uses (0=unlimited)</label><input type="number" min={0} value={discMaxUses} onChange={e => setDiscMaxUses(e.target.value)} /></div>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Create Discount</button>
                </form>
              </div>
              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <h3>Discounts ({discounts.length})</h3>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Name</th><th>Type</th><th>Value</th><th>Code</th><th>Used</th><th>Status</th><th>Actions</th></tr></thead>
                    <tbody>
                      {discounts.map(d => (
                        <tr key={d.id}><td>{d.id}</td><td>{d.name}</td><td>{d.type}</td>
                          <td>{d.type === 'percent' ? d.value + '%' : centsToDisplay(d.value)}</td>
                          <td>{d.promo_code || '—'}</td><td>{d.used_count}{d.max_uses > 0 ? '/' + d.max_uses : ''}</td>
                          <td><span className={`status-badge ${d.is_active ? 'active' : 'inactive'}`}>{d.is_active ? 'Active' : 'Inactive'}</span></td>
                          <td>{d.is_active && <button className="btn btn-xs btn-danger" onClick={() => onDeactivateDiscount(d.id)}>Deactivate</button>}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ SALES ============ */}
          {tab === 'sales' && (
            <div className="admin-section">
              <div className="admin-card">
                <div className="card-header"><h3>Recent Sales ({sales.length})</h3><button className="btn btn-ghost btn-sm" onClick={onRefreshSales}>↻</button></div>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Receipt</th><th>Subtotal</th><th>Tax</th><th>Discount</th><th>Total</th><th>Method</th><th>Cashier</th><th>Date</th></tr></thead>
                    <tbody>
                      {sales.map(s => (
                        <tr key={s.id}><td>{s.id}</td><td className="mono">{s.receipt_number}</td>
                          <td>{centsToDisplay(s.subtotal_cents)}</td><td>{centsToDisplay(s.tax_cents)}</td>
                          <td>{s.discount_cents > 0 ? '-' + centsToDisplay(s.discount_cents) : '—'}</td>
                          <td><strong>{centsToDisplay(s.total_cents)}</strong></td>
                          <td>{s.payment_method}</td><td>{s.sold_by}</td>
                          <td>{new Date(s.created_at).toLocaleString()}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ REFUNDS ============ */}
          {tab === 'refunds' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Process Refund</h3>
                <form className="admin-form" onSubmit={submitRefund}>
                  <div className="form-row">
                    <div className="form-group"><label>Sale ID</label><input type="number" min={1} required value={refundSaleId} onChange={e => setRefundSaleId(e.target.value)} /></div>
                    <div className="form-group"><label>Reason</label><input value={refundReason} onChange={e => setRefundReason(e.target.value)} placeholder="Customer request" /></div>
                  </div>
                  <div className="form-group"><label>Items to Refund</label>
                    {refundItems.map((ri, idx) => (
                      <div key={idx} className="form-row" style={{ marginBottom: 4 }}>
                        <input type="number" min={1} placeholder="Product ID" value={ri.product_id} onChange={e => { const next = [...refundItems]; next[idx].product_id = e.target.value; setRefundItems(next); }} />
                        <input type="number" min={1} placeholder="Qty" value={ri.quantity} onChange={e => { const next = [...refundItems]; next[idx].quantity = e.target.value; setRefundItems(next); }} style={{ width: 80 }} />
                        {idx > 0 && <button type="button" className="btn btn-xs btn-danger" onClick={() => setRefundItems(prev => prev.filter((_, i) => i !== idx))}>✕</button>}
                      </div>
                    ))}
                    <button type="button" className="btn btn-xs" onClick={() => setRefundItems(prev => [...prev, { product_id: '', quantity: '1' }])}>+ Add Item</button>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Process Refund</button>
                </form>
              </div>
              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <h3>Refund History ({refunds.length})</h3>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Sale</th><th>Receipt</th><th>Amount</th><th>Reason</th><th>Date</th></tr></thead>
                    <tbody>
                      {refunds.map(r => (
                        <tr key={r.id}><td>{r.id}</td><td>#{r.sale_id}</td><td className="mono">{r.receipt_number}</td>
                          <td>{centsToDisplay(r.total_cents)}</td><td>{r.reason || '—'}</td>
                          <td>{new Date(r.created_at).toLocaleString()}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}

          {/* ============ REPORTS ============ */}
          {tab === 'reports' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Sales Report</h3>
                <form className="admin-form inline-form" onSubmit={handleFetchReport}>
                  <input type="date" value={reportStart} onChange={e => setReportStart(e.target.value)} />
                  <span>to</span>
                  <input type="date" value={reportEnd} onChange={e => setReportEnd(e.target.value)} />
                  <button type="submit" className="btn btn-primary" disabled={busy}>Generate</button>
                </form>
              </div>

              {salesReport && (
                <>
                  {/* Summary Cards */}
                  <div className="report-summary-grid">
                    <div className="report-card accent-blue"><div className="report-card-value">{reportTotals!.totalSales}</div><div className="report-card-label">Total Sales</div></div>
                    <div className="report-card accent-green"><div className="report-card-value">{centsToDisplay(reportTotals!.totalRevenue)}</div><div className="report-card-label">Revenue</div></div>
                    <div className="report-card accent-yellow"><div className="report-card-value">{centsToDisplay(reportTotals!.totalCash)}</div><div className="report-card-label">Cash</div></div>
                    <div className="report-card accent-purple"><div className="report-card-value">{centsToDisplay(reportTotals!.totalCard)}</div><div className="report-card-label">Card</div></div>
                  </div>

                  {/* Revenue Chart */}
                  <div className="admin-card" style={{ marginTop: '1rem' }}>
                    <h3>Daily Revenue</h3>
                    <div className="chart-container">
                      {salesReport.report.map((row, i) => (
                        <div key={i} className="chart-bar-group">
                          <div className="chart-bar" style={{ height: `${Math.max(2, (row.revenue_cents / maxRevenue) * 100)}%` }} title={`${row.date}: ${centsToDisplay(row.revenue_cents)}`} />
                          <span className="chart-label">{row.date.slice(5)}</span>
                        </div>
                      ))}
                    </div>
                  </div>

                  {/* Top Products */}
                  <div className="admin-card" style={{ marginTop: '1rem' }}>
                    <h3>Top Products</h3>
                    <div className="data-table-wrap">
                      <table className="data-table">
                        <thead><tr><th>#</th><th>Product</th><th>Qty Sold</th><th>Revenue</th></tr></thead>
                        <tbody>
                          {salesReport.top_products.map((tp, i) => (
                            <tr key={tp.product_id}><td>{i + 1}</td><td>{tp.product_name}</td><td>{tp.total_quantity}</td><td>{centsToDisplay(tp.total_revenue_cents)}</td></tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                  </div>
                </>
              )}
            </div>
          )}

          {/* ============ USERS ============ */}
          {tab === 'users' && (
            <div className="admin-section">
              <div className="admin-card">
                <h3>Create User</h3>
                <form className="admin-form" onSubmit={submitCreateUser}>
                  <div className="form-row">
                    <div className="form-group"><label>Username</label><input required value={newUsername} onChange={e => setNewUsername(e.target.value)} /></div>
                    <div className="form-group"><label>Password</label><input type="password" required minLength={8} value={newPassword} onChange={e => setNewPassword(e.target.value)} /></div>
                    <div className="form-group"><label>Role</label>
                      <select value={newRole} onChange={e => setNewRole(e.target.value as 'admin' | 'cashier')}>
                        <option value="cashier">Cashier</option><option value="admin">Admin</option>
                      </select>
                    </div>
                  </div>
                  <button type="submit" className="btn btn-primary" disabled={busy}>Create User</button>
                </form>
              </div>
              <div className="admin-card" style={{ marginTop: '1rem' }}>
                <div className="card-header"><h3>Users ({users.length})</h3><button className="btn btn-ghost btn-sm" onClick={onRefreshUsers}>↻</button></div>
                <div className="data-table-wrap">
                  <table className="data-table">
                    <thead><tr><th>ID</th><th>Username</th><th>Role</th><th>Active</th></tr></thead>
                    <tbody>
                      {users.map(u => (
                        <tr key={u.id}><td>{u.id}</td><td>{u.username}</td><td>{u.role}</td>
                          <td><span className={`status-badge ${u.is_active ? 'active' : 'inactive'}`}>{u.is_active ? 'Yes' : 'No'}</span></td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
