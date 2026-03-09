import { FormEvent, useMemo, useState } from 'react';

import type { CartItem, Product, User } from '../types';

type PaymentMethod = 'cash' | 'card' | 'transfer';

interface PosPageProps {
  user: User;
  products: Product[];
  busy: boolean;
  error: string | null;
  notice: string | null;
  onLogout: () => Promise<void>;
  onRefreshProducts: () => Promise<void>;
  onCheckout: (input: { payment_method: PaymentMethod; items: CartItem[] }) => Promise<void>;
  onNavigate: (page: 'pos' | 'admin') => void;
}

function centsToDisplay(cents: number): string {
  return `$${(cents / 100).toFixed(2)}`;
}

export function PosPage({
  user,
  products,
  busy,
  error,
  notice,
  onLogout,
  onRefreshProducts,
  onCheckout,
  onNavigate,
}: PosPageProps) {
  const [paymentMethod, setPaymentMethod] = useState<PaymentMethod>('cash');
  const [cart, setCart] = useState<Record<number, number>>({});
  const [search, setSearch] = useState('');

  const filteredProducts = useMemo(() => {
    if (!search.trim()) return products;
    const q = search.toLowerCase();
    return products.filter(
      p => p.name.toLowerCase().includes(q) || p.sku.toLowerCase().includes(q)
    );
  }, [products, search]);

  const cartDetails = useMemo(() => {
    return Object.entries(cart)
      .map(([idText, quantity]) => {
        const productId = Number(idText);
        const product = products.find(p => p.id === productId);
        if (!product || quantity <= 0) return null;
        return { product, quantity, lineTotal: quantity * product.price_cents };
      })
      .filter((e): e is { product: Product; quantity: number; lineTotal: number } => e !== null);
  }, [cart, products]);

  const cartTotalCents = useMemo(
    () => cartDetails.reduce((t, e) => t + e.lineTotal, 0),
    [cartDetails],
  );

  const cartItemCount = useMemo(
    () => cartDetails.reduce((t, e) => t + e.quantity, 0),
    [cartDetails],
  );

  function addToCart(product: Product) {
    if (product.stock_quantity <= 0) return;
    setCart(c => {
      const existing = c[product.id] ?? 0;
      if (existing >= product.stock_quantity) return c;
      return { ...c, [product.id]: existing + 1 };
    });
  }

  function removeFromCart(productId: number) {
    setCart(c => {
      const next = { ...c };
      delete next[productId];
      return next;
    });
  }

  function decrementInCart(productId: number) {
    setCart(c => {
      const existing = c[productId] ?? 0;
      if (existing <= 1) {
        const next = { ...c };
        delete next[productId];
        return next;
      }
      return { ...c, [productId]: existing - 1 };
    });
  }

  function incrementInCart(product: Product) {
    setCart(c => {
      const existing = c[product.id] ?? 0;
      if (existing >= product.stock_quantity) return c;
      return { ...c, [product.id]: existing + 1 };
    });
  }

  function clearCart() {
    setCart({});
  }

  async function submitCheckout(e: FormEvent) {
    e.preventDefault();
    if (cartDetails.length === 0) return;
    const items: CartItem[] = cartDetails.map(entry => ({
      product_id: entry.product.id,
      quantity: entry.quantity,
    }));
    await onCheckout({ payment_method: paymentMethod, items });
    setCart({});
  }

  return (
    <div className="pos-shell">
      {/* Left: product area */}
      <div className="pos-products-area">
        {/* Top bar */}
        <header className="pos-header">
          <div className="pos-header-left">
            <div className="pos-brand">
              <div className="brand-icon">POS</div>
              <div>
                <h1>Point of Sale</h1>
                <span className="pos-operator">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
                  {user.username} &middot; {user.role}
                </span>
              </div>
            </div>
          </div>
          <div className="pos-header-right">
            {user.role === 'admin' && (
              <button className="btn btn-ghost" onClick={() => onNavigate('admin')}>
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
                Admin
              </button>
            )}
            <button className="btn btn-ghost" onClick={onRefreshProducts} disabled={busy}>
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/></svg>
            </button>
            <button className="btn btn-ghost-danger" onClick={onLogout} disabled={busy}>
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
            </button>
          </div>
        </header>

        {/* Alerts */}
        {error && <div className="pos-alert pos-alert-error">{error}</div>}
        {notice && <div className="pos-alert pos-alert-success">{notice}</div>}

        {/* Search bar */}
        <div className="pos-search">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
          <input
            type="text"
            placeholder="Search products by name or SKU..."
            value={search}
            onChange={e => setSearch(e.target.value)}
          />
        </div>

        {/* Product grid */}
        <div className="pos-product-grid">
          {filteredProducts.map(product => (
            <button
              key={product.id}
              className={`pos-product-card ${product.stock_quantity <= 0 ? 'out-of-stock' : ''}`}
              onClick={() => addToCart(product)}
              disabled={busy || product.stock_quantity <= 0}
              type="button"
            >
              <div className="pos-product-img">
                {product.image_url ? (
                  <img src={product.image_url} alt={product.name} />
                ) : (
                  <div className="pos-product-img-placeholder">
                    <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
                  </div>
                )}
                {product.stock_quantity <= 0 && (
                  <div className="pos-product-badge-oos">Out of Stock</div>
                )}
                {product.stock_quantity > 0 && product.stock_quantity <= 5 && (
                  <div className="pos-product-badge-low">Low: {product.stock_quantity}</div>
                )}
                {(cart[product.id] ?? 0) > 0 && (
                  <div className="pos-product-cart-qty">{cart[product.id]}</div>
                )}
              </div>
              <div className="pos-product-info">
                <span className="pos-product-name">{product.name}</span>
                <span className="pos-product-price">{centsToDisplay(product.price_cents)}</span>
              </div>
            </button>
          ))}
          {filteredProducts.length === 0 && (
            <div className="pos-empty">No products found</div>
          )}
        </div>
      </div>

      {/* Right: cart sidebar */}
      <aside className="pos-cart-sidebar">
        <div className="pos-cart-header">
          <h2>
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="9" cy="21" r="1"/><circle cx="20" cy="21" r="1"/><path d="M1 1h4l2.68 13.39a2 2 0 0 0 2 1.61h9.72a2 2 0 0 0 2-1.61L23 6H6"/></svg>
            Cart
          </h2>
          <span className="cart-count">{cartItemCount} items</span>
        </div>

        {/* Cart items */}
        <div className="pos-cart-items">
          {cartDetails.length === 0 && (
            <div className="pos-cart-empty">
              <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1"><circle cx="9" cy="21" r="1"/><circle cx="20" cy="21" r="1"/><path d="M1 1h4l2.68 13.39a2 2 0 0 0 2 1.61h9.72a2 2 0 0 0 2-1.61L23 6H6"/></svg>
              <p>Tap a product to add it</p>
            </div>
          )}
          {cartDetails.map(entry => (
            <div className="pos-cart-item" key={entry.product.id}>
              <div className="pos-cart-item-info">
                <span className="pos-cart-item-name">{entry.product.name}</span>
                <span className="pos-cart-item-unit">{centsToDisplay(entry.product.price_cents)} each</span>
              </div>
              <div className="pos-cart-item-controls">
                <button type="button" className="qty-btn" onClick={() => decrementInCart(entry.product.id)} disabled={busy}>-</button>
                <span className="qty-display">{entry.quantity}</span>
                <button type="button" className="qty-btn" onClick={() => incrementInCart(entry.product)} disabled={busy || entry.quantity >= entry.product.stock_quantity}>+</button>
              </div>
              <div className="pos-cart-item-total">
                <span>{centsToDisplay(entry.lineTotal)}</span>
                <button type="button" className="cart-remove-btn" onClick={() => removeFromCart(entry.product.id)} disabled={busy} title="Remove">
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                </button>
              </div>
            </div>
          ))}
        </div>

        {/* Checkout section */}
        <form className="pos-checkout" onSubmit={submitCheckout}>
          {cartDetails.length > 0 && (
            <button type="button" className="btn btn-ghost btn-small" onClick={clearCart}>Clear Cart</button>
          )}

          <div className="pos-payment-methods">
            {(['cash', 'card', 'transfer'] as const).map(method => (
              <button
                key={method}
                type="button"
                className={`payment-method-btn ${paymentMethod === method ? 'selected' : ''}`}
                onClick={() => setPaymentMethod(method)}
              >
                {method === 'cash' && (
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="1" y="4" width="22" height="16" rx="2" ry="2"/><line x1="1" y1="10" x2="23" y2="10"/></svg>
                )}
                {method === 'card' && (
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="1" y="4" width="22" height="16" rx="2" ry="2"/><line x1="1" y1="10" x2="23" y2="10"/></svg>
                )}
                {method === 'transfer' && (
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="12" y1="1" x2="12" y2="23"/><path d="M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"/></svg>
                )}
                {method.charAt(0).toUpperCase() + method.slice(1)}
              </button>
            ))}
          </div>

          <div className="pos-total">
            <span>Total</span>
            <strong>{centsToDisplay(cartTotalCents)}</strong>
          </div>

          <button type="submit" className="btn btn-checkout" disabled={busy || cartDetails.length === 0}>
            {busy ? 'Processing...' : 'Charge ' + centsToDisplay(cartTotalCents)}
          </button>
        </form>
      </aside>
    </div>
  );
}
