# Secure POS System (C++ + Docker + PostgreSQL + React)

This project is a clean starter for a secure point-of-sale system with:
- `backend/`: C++20 API server (`cpp-httplib`, `libpqxx`, OpenSSL)
- `docker/postgres/init/`: PostgreSQL schema and constraints
- `frontend/`: React + TypeScript operator console
- `docker-compose.yml`: full stack orchestration

## Security Design Highlights

- PBKDF2-HMAC-SHA256 password hashing with configurable iteration count
- Session tokens generated from CSPRNG, stored in DB only as SHA-256 hashes
- HttpOnly session cookie (`SameSite=Strict`, optional `Secure`)
- Role-based access controls (`admin`, `cashier`)
- Prepared SQL statements for all DB operations
- Transactional stock decrement + sale creation (atomic)
- Login rate limiting by client IP
- API security headers (`X-Frame-Options`, `X-Content-Type-Options`, `Referrer-Policy`, `Permissions-Policy`)
- Audit log table for security-relevant actions

## Quick Start

1. Create environment file:
   - `copy .env.example .env`
2. Set secure values in `.env`:
   - `POSTGRES_PASSWORD`
   - `SESSION_TOKEN_PEPPER` (recommended: `openssl rand -hex 32`)
   - `BOOTSTRAP_ADMIN_PASSWORD`
3. Build and run:

```bash
docker compose up --build
```

4. Open frontend:
   - `http://localhost:3000`

5. API health:
   - `http://localhost:8080/api/health`

## Default Bootstrap Flow

- If `BOOTSTRAP_ADMIN_USERNAME` and `BOOTSTRAP_ADMIN_PASSWORD` are set, backend creates the admin user on startup when missing.
- Remove or rotate bootstrap credentials after first secure login.

## API Overview

- `POST /api/auth/login`
- `POST /api/auth/logout`
- `GET /api/me`
- `GET /api/products`
- `POST /api/products` (admin)
- `PATCH /api/products/:id/stock` (admin)
- `POST /api/sales` (cashier/admin)
- `GET /api/sales?limit=20` (admin)

## Structure

```text
.
├─ backend/
│  ├─ include/pos/
│  ├─ src/
│  └─ Dockerfile
├─ docker/
│  └─ postgres/init/001_schema.sql
├─ frontend/
│  ├─ src/
│  ├─ nginx/default.conf
│  └─ Dockerfile
├─ docker-compose.yml
└─ .env.example
```

## Hardening Notes for Production

- Run behind TLS and set `COOKIE_SECURE=true`
- Restrict `CORS_ORIGIN` to the exact production origin
- Use managed secrets (not plain `.env` on shared hosts)
- Add database backups, secret rotation, and centralized audit ingestion
- Add automated tests and SAST/DAST in CI before deployment