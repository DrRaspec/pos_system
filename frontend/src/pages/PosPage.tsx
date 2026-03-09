import { FormEvent, useEffect, useMemo, useRef, useState } from 'react';

import type { CartItem, Category, Customer, Discount, Product, Sale, SalePayment, Shift, User } from '../types';

type PaymentMethod = 'cash' | 'card' | 'transfer' | 'split';

interface PosPageProps {
  user: User;
  products: Product[];
  categories: Category[];
  customers: Customer[];
  shift: Shift | null;
  busy: boolean;
  error: string | null;
  notice: string | null;
  onLogout: () => Promise<void>;
  onRefreshProducts: () => Promise<void>;
  onCheckout: (input: {
    payment_method: PaymentMethod;
    items: CartItem[];
    customer_id?: number;
    discount_id?: number;
    shift_id?: number;
    payments?: SalePayment[];
  }) => Promise<Sale | void>;
  onNavigate: (page: 'pos' | 'admin') => void;
  onOpenShift: (openingCash: number) => Promise<void>;
  onCloseShift: (shiftId: number, closingCash: number, notes: string) => Promise<void>;
  onValidatePromo: (code: string) => Promise<Discount | null>;
  onFindCustomerByPhone: (phone: string) => Promise<Customer | null>;
  onCreateCustomer: (input: { name: string; phone?: string }) => Promise<void>;
  onFindByBarcode: (barcode: string) => Promise<Product | null>;
}

function centsToDisplay(cents: number): string {
  return '$' + (cents / 100).toFixed(2);
}

export function PosPage({
  user,
  products,
  categories,
  customers: _customers,
  shift,
  busy,
  error,
  notice,
  onLogout,
  onRefreshProducts,
  onCheckout,
  onNavigate,
  onOpenShift,
  onCloseShift,
  onValidatePromo,
  onFindCustomerByPhone,
  onCreateCustomer,
  onFindByBarcode,
}: PosPageProps) {
  const [cart, setCart] = useState<CartItem[]>([]);
  const [paymentMethod, setPaymentMethod] = useState<PaymentMethod>('cash');
  const [searchQuery, setSearchQuery] = useState('');
  const [selectedCategory, setSelectedCategory] = useState<number>(0);

  // Customer
  const [customerPhone, setCustomerPhone] = useState('');
  const [selectedCustomer, setSelectedCustomer] = useState<Customer | null>(null);
  const [showNewCustomer, setShowNewCustomer] = useState(false);
  const [newCustName, setNewCustName] = useState('');

  // Discount
  const [promoCode, setPromoCode] = useState('');
  const [appliedDiscount, setAppliedDiscount] = useState<Discount | null>(null);

  // Split Payment
  const [splitPayments, setSplitPayments] = useState<SalePayment[]>([]);
  const [splitMethod, setSplitMethod] = useState<'cash' | 'card' | 'transfer'>('cash');
  const [splitAmount, setSplitAmount] = useState('');

  // Cash change
  const [cashReceived, setCashReceived] = useState('');

  // Receipt
  const [lastSale, setLastSale] = useState<Sale | null>(null);
  const [showReceipt, setShowReceipt] = useState(false);

  // Shift
  const [showShiftOpen, setShowShiftOpen] = useState(false);
  const [shiftOpenCash, setShiftOpenCash] = useState('');
  const [showShiftClose, setShowShiftClose] = useState(false);
  const [shiftCloseCash, setShiftCloseCash] = useState('');
  const [shiftCloseNotes, setShiftCloseNotes] = useState('');

  // Barcode
  const barcodeInputRef = useRef<HTMLInputElement>(null);
  const [barcodeValue, setBarcodeValue] = useState('');

  const filteredProducts = useMemo(() => {
    let filtered = products;
    if (selectedCategory > 0) {
      filtered = filtered.filter(p => p.category_id === selectedCategory);
    }
    if (searchQuery.trim()) {
      const q = searchQuery.toLowerCase();
      filtered = filtered.filter(p => p.name.toLowerCase().includes(q) || p.sku.toLowerCase().includes(q));
    }
    return filtered;
  }, [products, selectedCategory, searchQuery]);

  const cartDetails = useMemo(() => {
    return cart.map(ci => {
      const product = products.find(p => p.id === ci.product_id);
      if (!product) return null;
      const lineSubtotal = product.price_cents * ci.quantity;
      const lineTax = Math.round(lineSubtotal * product.tax_rate_percent / 100);
      return { ...ci, product, lineSubtotal, lineTax, lineTotal: lineSubtotal + lineTax };
    }).filter(Boolean) as { product_id: number; quantity: number; product: Product; lineSubtotal: number; lineTax: number; lineTotal: number }[];
  }, [cart, products]);

  const subtotal = cartDetails.reduce((s, c) => s + c.lineSubtotal, 0);
  const totalTax = cartDetails.reduce((s, c) => s + c.lineTax, 0);

  const discountAmount = useMemo(() => {
    if (!appliedDiscount) return 0;
    if (appliedDiscount.min_order_cents > subtotal) return 0;
    if (appliedDiscount.type === 'percent') return Math.round(subtotal * appliedDiscount.value / 100);
    return Math.min(appliedDiscount.value, subtotal + totalTax);
  }, [appliedDiscount, subtotal, totalTax]);

  const grandTotal = subtotal + totalTax - discountAmount;

  const splitTotal = splitPayments.reduce((s, p) => s + p.amount_cents, 0);
  const splitRemaining = grandTotal - splitTotal;

  const cashReceivedCents = Math.round(parseFloat(cashReceived || '0') * 100);
  const changeDue = paymentMethod === 'cash' ? Math.max(0, cashReceivedCents - grandTotal) : 0;

  // Focus barcode input on mount
  useEffect(() => { barcodeInputRef.current?.focus(); }, []);

  function addToCart(product: Product) {
    setCart(prev => {
      const existing = prev.find(c => c.product_id === product.id);
      if (existing) {
        if (existing.quantity >= product.stock_quantity) return prev;
        return prev.map(c => c.product_id === product.id ? { ...c, quantity: c.quantity + 1 } : c);
      }
      if (product.stock_quantity <= 0) return prev;
      return [...prev, { product_id: product.id, quantity: 1 }];
    });
  }

  function removeFromCart(productId: number) {
    setCart(prev => prev.filter(c => c.product_id !== productId));
  }

  function decrementInCart(productId: number) {
    setCart(prev => {
      const existing = prev.find(c => c.product_id === productId);
      if (!existing) return prev;
      if (existing.quantity <= 1) return prev.filter(c => c.product_id !== productId);
      return prev.map(c => c.product_id === productId ? { ...c, quantity: c.quantity - 1 } : c);
    });
  }

  function incrementInCart(product: Product) {
    setCart(prev => {
      const existing = prev.find(c => c.product_id === product.id);
      if (!existing || existing.quantity >= product.stock_quantity) return prev;
      return prev.map(c => c.product_id === product.id ? { ...c, quantity: c.quantity + 1 } : c);
    });
  }

  function clearCart() { setCart([]); setAppliedDiscount(null); setPromoCode(''); setSelectedCustomer(null); setCustomerPhone(''); setSplitPayments([]); setCashReceived(''); }

  async function handleBarcodeScan(e: FormEvent) {
    e.preventDefault();
    if (!barcodeValue.trim()) return;
    const product = await onFindByBarcode(barcodeValue.trim());
    if (product) addToCart(product);
    setBarcodeValue('');
    barcodeInputRef.current?.focus();
  }

  async function handleCustomerLookup() {
    if (!customerPhone.trim()) return;
    const cust = await onFindCustomerByPhone(customerPhone.trim());
    if (cust) {
      setSelectedCustomer(cust);
      setShowNewCustomer(false);
    } else {
      setShowNewCustomer(true);
    }
  }

  async function handleCreateNewCustomer(e: FormEvent) {
    e.preventDefault();
    await onCreateCustomer({ name: newCustName, phone: customerPhone });
    const cust = await onFindCustomerByPhone(customerPhone);
    if (cust) setSelectedCustomer(cust);
    setNewCustName('');
    setShowNewCustomer(false);
  }

  async function handleApplyPromo() {
    if (!promoCode.trim()) return;
    const disc = await onValidatePromo(promoCode.trim());
    setAppliedDiscount(disc);
  }

  function addSplitPayment() {
    const amount = Math.round(parseFloat(splitAmount || '0') * 100);
    if (amount <= 0 || amount > splitRemaining) return;
    setSplitPayments(prev => [...prev, { method: splitMethod, amount_cents: amount }]);
    setSplitAmount('');
  }

  async function submitCheckout(e: FormEvent) {
    e.preventDefault();
    if (cart.length === 0) return;
    if (paymentMethod === 'split' && splitRemaining > 0) return;
    if (paymentMethod === 'cash' && cashReceivedCents < grandTotal) return;

    const result = await onCheckout({
      payment_method: paymentMethod,
      items: cart,
      customer_id: selectedCustomer?.id,
      discount_id: appliedDiscount?.id,
      shift_id: shift?.id,
      payments: paymentMethod === 'split' ? splitPayments : undefined,
    });
    if (result) {
      setLastSale(result);
      setShowReceipt(true);
    }
    setCart([]);
    setAppliedDiscount(null);
    setPromoCode('');
    setSelectedCustomer(null);
    setCustomerPhone('');
    setSplitPayments([]);
    setCashReceived('');
  }

  async function handleOpenShift(e: FormEvent) {
    e.preventDefault();
    const cents = Math.round(parseFloat(shiftOpenCash || '0') * 100);
    await onOpenShift(cents);
    setShowShiftOpen(false);
    setShiftOpenCash('');
  }

  async function handleCloseShift(e: FormEvent) {
    e.preventDefault();
    if (!shift) return;
    const cents = Math.round(parseFloat(shiftCloseCash || '0') * 100);
    await onCloseShift(shift.id, cents, shiftCloseNotes);
    setShowShiftClose(false);
    setShiftCloseCash('');
    setShiftCloseNotes('');
  }

  return (
    <div className="pos-layout">
      {/* Header */}
      <header className="pos-header">
        <div className="header-left">
          <div className="brand-icon">POS</div>
          <span className="header-title">Point of Sale</span>
          {shift && <span className="shift-badge">Shift #{shift.id}</span>}
        </div>
        <div className="header-right">
          <span className="user-label">{user.username} ({user.role})</span>
          {user.role === 'admin' && <button className="btn btn-ghost" onClick={() => onNavigate('admin')}>Admin</button>}
          {!shift ? (
            <button className="btn btn-outline" onClick={() => setShowShiftOpen(true)}>Open Shift</button>
          ) : (
            <button className="btn btn-outline" onClick={() => setShowShiftClose(true)}>Close Shift</button>
          )}
          <button className="btn btn-ghost" onClick={onLogout}>Logout</button>
        </div>
      </header>

      {/* Notifications */}
      {error && <div className="alert alert-error">{error}</div>}
      {notice && <div className="alert alert-success">{notice}</div>}

      <div className="pos-body">
        {/* Product Panel */}
        <div className="pos-products-panel">
          {/* Search + Barcode */}
          <div className="pos-search-row">
            <input type="text" className="search-input" placeholder="Search products..." value={searchQuery} onChange={e => setSearchQuery(e.target.value)} />
            <form className="barcode-form" onSubmit={handleBarcodeScan}>
              <input ref={barcodeInputRef} type="text" className="barcode-input" placeholder="Scan barcode..." value={barcodeValue} onChange={e => setBarcodeValue(e.target.value)} />
              <button type="submit" className="btn btn-sm">Scan</button>
            </form>
            <button className="btn btn-ghost btn-sm" onClick={onRefreshProducts}>↻</button>
          </div>

          {/* Category Tabs */}
          <div className="category-tabs">
            <button className={`category-tab ${selectedCategory === 0 ? 'active' : ''}`} onClick={() => setSelectedCategory(0)}>All</button>
            {categories.map(cat => (
              <button key={cat.id} className={`category-tab ${selectedCategory === cat.id ? 'active' : ''}`} style={{ '--cat-color': cat.color } as React.CSSProperties} onClick={() => setSelectedCategory(cat.id)}>{cat.name}</button>
            ))}
          </div>

          {/* Product Grid */}
          <div className="pos-product-grid">
            {filteredProducts.map(product => {
              const inCart = cart.find(c => c.product_id === product.id);
              return (
                <button key={product.id} className={`product-card ${product.stock_quantity <= 0 ? 'out-of-stock' : ''} ${inCart ? 'in-cart' : ''}`} onClick={() => addToCart(product)} disabled={product.stock_quantity <= 0}>
                  {product.image_url ? (
                    <img src={product.image_url} alt={product.name} className="product-card-img" />
                  ) : (
                    <div className="product-card-placeholder">{product.name.charAt(0)}</div>
                  )}
                  <div className="product-card-info">
                    <span className="product-card-name">{product.name}</span>
                    <span className="product-card-price">{centsToDisplay(product.price_cents)}</span>
                    <span className="product-card-stock">Stock: {product.stock_quantity}</span>
                    {product.tax_rate_percent > 0 && <span className="product-card-tax">+{product.tax_rate_percent}% tax</span>}
                  </div>
                  {inCart && <span className="product-card-badge">{inCart.quantity}</span>}
                </button>
              );
            })}
            {filteredProducts.length === 0 && <p className="empty-state">No products found.</p>}
          </div>
        </div>

        {/* Cart Panel */}
        <div className="pos-cart-panel">
          <h2 className="cart-title">Cart ({cart.reduce((s, c) => s + c.quantity, 0)} items)</h2>

          <div className="cart-items">
            {cartDetails.map(item => (
              <div key={item.product_id} className="cart-item">
                <div className="cart-item-info">
                  <span className="cart-item-name">{item.product.name}</span>
                  <span className="cart-item-price">{centsToDisplay(item.product.price_cents)} × {item.quantity}</span>
                </div>
                <div className="cart-item-actions">
                  <button className="btn btn-xs" onClick={() => decrementInCart(item.product_id)}>−</button>
                  <span className="cart-item-qty">{item.quantity}</span>
                  <button className="btn btn-xs" onClick={() => incrementInCart(item.product)}>+</button>
                  <button className="btn btn-xs btn-danger" onClick={() => removeFromCart(item.product_id)}>✕</button>
                </div>
                <span className="cart-item-total">{centsToDisplay(item.lineTotal)}</span>
              </div>
            ))}
            {cart.length === 0 && <p className="empty-state">Cart is empty</p>}
          </div>

          {cart.length > 0 && (
            <>
              {/* Customer */}
              <div className="cart-section">
                <label className="cart-section-label">Customer (optional)</label>
                {selectedCustomer ? (
                  <div className="selected-tag">
                    <span>{selectedCustomer.name} — {selectedCustomer.loyalty_points} pts</span>
                    <button className="btn btn-xs" onClick={() => { setSelectedCustomer(null); setCustomerPhone(''); }}>✕</button>
                  </div>
                ) : (
                  <div className="inline-form">
                    <input type="text" placeholder="Phone number" value={customerPhone} onChange={e => setCustomerPhone(e.target.value)} />
                    <button className="btn btn-sm" onClick={handleCustomerLookup}>Find</button>
                  </div>
                )}
                {showNewCustomer && (
                  <form className="inline-form" onSubmit={handleCreateNewCustomer} style={{ marginTop: 4 }}>
                    <input type="text" placeholder="Customer name" value={newCustName} onChange={e => setNewCustName(e.target.value)} required />
                    <button type="submit" className="btn btn-sm btn-primary">Add</button>
                  </form>
                )}
              </div>

              {/* Promo Code */}
              <div className="cart-section">
                <label className="cart-section-label">Promo Code</label>
                {appliedDiscount ? (
                  <div className="selected-tag discount-tag">
                    <span>{appliedDiscount.name} ({appliedDiscount.type === 'percent' ? appliedDiscount.value + '%' : centsToDisplay(appliedDiscount.value)} off)</span>
                    <button className="btn btn-xs" onClick={() => { setAppliedDiscount(null); setPromoCode(''); }}>✕</button>
                  </div>
                ) : (
                  <div className="inline-form">
                    <input type="text" placeholder="Enter code" value={promoCode} onChange={e => setPromoCode(e.target.value)} />
                    <button className="btn btn-sm" onClick={handleApplyPromo}>Apply</button>
                  </div>
                )}
              </div>

              {/* Summary */}
              <div className="cart-summary">
                <div className="summary-row"><span>Subtotal</span><span>{centsToDisplay(subtotal)}</span></div>
                {totalTax > 0 && <div className="summary-row"><span>Tax</span><span>+{centsToDisplay(totalTax)}</span></div>}
                {discountAmount > 0 && <div className="summary-row discount-row"><span>Discount</span><span>−{centsToDisplay(discountAmount)}</span></div>}
                <div className="summary-row total-row"><span>Total</span><span>{centsToDisplay(grandTotal)}</span></div>
              </div>

              {/* Payment */}
              <form className="cart-checkout" onSubmit={submitCheckout}>
                <div className="payment-methods">
                  {(['cash', 'card', 'transfer', 'split'] as PaymentMethod[]).map(m => (
                    <button key={m} type="button" className={`payment-btn ${paymentMethod === m ? 'active' : ''}`} onClick={() => { setPaymentMethod(m); setSplitPayments([]); }}>
                      {m === 'cash' ? '💵 Cash' : m === 'card' ? '💳 Card' : m === 'transfer' ? '🏛 Transfer' : '🔀 Split'}
                    </button>
                  ))}
                </div>

                {paymentMethod === 'cash' && (
                  <div className="cash-section">
                    <label>Cash Received</label>
                    <input type="number" step="0.01" min="0" placeholder="0.00" value={cashReceived} onChange={e => setCashReceived(e.target.value)} />
                    {cashReceivedCents > 0 && <div className="change-display">Change: <strong>{centsToDisplay(changeDue)}</strong></div>}
                  </div>
                )}

                {paymentMethod === 'split' && (
                  <div className="split-section">
                    <div className="split-list">
                      {splitPayments.map((sp, i) => (
                        <div key={i} className="split-item">
                          <span>{sp.method}: {centsToDisplay(sp.amount_cents)}</span>
                          <button type="button" className="btn btn-xs btn-danger" onClick={() => setSplitPayments(prev => prev.filter((_, idx) => idx !== i))}>✕</button>
                        </div>
                      ))}
                    </div>
                    {splitRemaining > 0 && (
                      <div className="split-add">
                        <select value={splitMethod} onChange={e => setSplitMethod(e.target.value as 'cash' | 'card' | 'transfer')}>
                          <option value="cash">Cash</option>
                          <option value="card">Card</option>
                          <option value="transfer">Transfer</option>
                        </select>
                        <input type="number" step="0.01" min="0.01" placeholder="Amount" value={splitAmount} onChange={e => setSplitAmount(e.target.value)} />
                        <button type="button" className="btn btn-sm" onClick={addSplitPayment}>Add</button>
                      </div>
                    )}
                    <div className="split-remaining">Remaining: <strong>{centsToDisplay(splitRemaining)}</strong></div>
                  </div>
                )}

                <div className="checkout-actions">
                  <button type="button" className="btn btn-outline" onClick={clearCart}>Clear</button>
                  <button type="submit" className="btn btn-primary btn-lg" disabled={busy || cart.length === 0 || (paymentMethod === 'split' && splitRemaining > 0) || (paymentMethod === 'cash' && cashReceivedCents < grandTotal)}>
                    Checkout {centsToDisplay(grandTotal)}
                  </button>
                </div>
              </form>
            </>
          )}
        </div>
      </div>

      {/* Shift Open Modal */}
      {showShiftOpen && (
        <div className="modal-overlay" onClick={() => setShowShiftOpen(false)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <h3>Open Shift</h3>
            <form onSubmit={handleOpenShift}>
              <div className="form-group">
                <label>Opening Cash ($)</label>
                <input type="number" step="0.01" min="0" value={shiftOpenCash} onChange={e => setShiftOpenCash(e.target.value)} autoFocus />
              </div>
              <div className="modal-actions">
                <button type="button" className="btn btn-outline" onClick={() => setShowShiftOpen(false)}>Cancel</button>
                <button type="submit" className="btn btn-primary">Open Shift</button>
              </div>
            </form>
          </div>
        </div>
      )}

      {/* Shift Close Modal */}
      {showShiftClose && shift && (
        <div className="modal-overlay" onClick={() => setShowShiftClose(false)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <h3>Close Shift #{shift.id}</h3>
            <p className="modal-info">Opened with: <strong>{centsToDisplay(shift.opening_cash_cents)}</strong></p>
            <form onSubmit={handleCloseShift}>
              <div className="form-group">
                <label>Closing Cash ($)</label>
                <input type="number" step="0.01" min="0" value={shiftCloseCash} onChange={e => setShiftCloseCash(e.target.value)} autoFocus />
              </div>
              <div className="form-group">
                <label>Notes</label>
                <textarea value={shiftCloseNotes} onChange={e => setShiftCloseNotes(e.target.value)} rows={2} />
              </div>
              <div className="modal-actions">
                <button type="button" className="btn btn-outline" onClick={() => setShowShiftClose(false)}>Cancel</button>
                <button type="submit" className="btn btn-primary">Close Shift</button>
              </div>
            </form>
          </div>
        </div>
      )}

      {/* Receipt Modal */}
      {showReceipt && lastSale && (
        <div className="modal-overlay" onClick={() => setShowReceipt(false)}>
          <div className="modal receipt-modal" onClick={e => e.stopPropagation()}>
            <div className="receipt" id="receipt-print">
              <div className="receipt-header">
                <h3>POS System</h3>
                <p>Receipt #{lastSale.receipt_number}</p>
                <p>{new Date(lastSale.created_at).toLocaleString()}</p>
              </div>
              <hr />
              <div className="receipt-summary">
                <div className="receipt-row"><span>Subtotal</span><span>{centsToDisplay(lastSale.subtotal_cents)}</span></div>
                {lastSale.tax_cents > 0 && <div className="receipt-row"><span>Tax</span><span>{centsToDisplay(lastSale.tax_cents)}</span></div>}
                {lastSale.discount_cents > 0 && <div className="receipt-row"><span>Discount</span><span>-{centsToDisplay(lastSale.discount_cents)}</span></div>}
                <div className="receipt-row receipt-total"><span>TOTAL</span><span>{centsToDisplay(lastSale.total_cents)}</span></div>
                <div className="receipt-row"><span>Payment</span><span>{lastSale.payment_method.toUpperCase()}</span></div>
                {paymentMethod === 'cash' && changeDue > 0 && <div className="receipt-row"><span>Change</span><span>{centsToDisplay(changeDue)}</span></div>}
              </div>
              <hr />
              <p className="receipt-footer">Thank you for your purchase!</p>
            </div>
            <div className="modal-actions">
              <button className="btn btn-outline" onClick={() => { window.print(); }}>🖨 Print</button>
              <button className="btn btn-primary" onClick={() => setShowReceipt(false)}>Done</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
