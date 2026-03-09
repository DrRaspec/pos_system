# Getting Started - POS System Setup & Usage

Hey! So you've got this POS system running. Here's what you actually need to know.

## What You Have

- **Backend**: C++ API server (port 8080)
- **Frontend**: React cashier interface (port 3000)  
- **Database**: PostgreSQL with automatic schema setup

## Initial Setup - Before You Start

### 1. Environment File (Critical!)
Copy `.env.example` to `.env`:
```bash
copy .env.example .env
```

**Important**: If your password has special characters like `#`, wrap it in quotes:
```
POSTGRES_PASSWORD="MyPass#WithSymbols"
```
Otherwise Docker will truncate it and authentication fails. (Yes, this caught us. Multiple times.)

### 2. Generate Security Values
Open PowerShell and generate a random pepper for session tokens:
```powershell
openssl rand -hex 32
```
Paste that into `SESSION_TOKEN_PEPPER` in `.env`.

Example result:
```
SESSION_TOKEN_PEPPER=A3F9D2E1B4C6F8A0D3E5F7A9B1C3D5E7F9A1B3C5D7E9F0A2B4C6D8E0F2A4
```

### 3. Set Bootstrap Credentials
In `.env`, set your initial admin account:
```
BOOTSTRAP_ADMIN_USERNAME=admin
BOOTSTRAP_ADMIN_PASSWORD=SomeSecurePassword123
```
You'll use these to log in the first time. Change them afterward.

## Bringing It Up

```bash
docker compose up --build
```

Wait for all three containers to show **Up** status:
```
db         ...  (healthy)
backend    ...  Up
frontend   ...  Up
```

If backend keeps restarting, check the logs:
```bash
docker compose logs backend --tail 50
```

**Common culprit**: Password truncation. Double-check `POSTGRES_PASSWORD` in `.env`.

## Actually Using It

### Login
1. Open browser: **http://localhost:3000**
2. Use your bootstrap credentials:
   - Username: `admin`
   - Password: whatever you set in `.env`

### What You Can Do

**As Admin**:
- Add/edit products
- Adjust stock quantities  
- View all sales history
- Change other users' passwords

**As Cashier**:
- Ring up sales
- See current inventory
- That's it (by design)

### Creating Users
You need to do this via the backend API (no UI for it yet). 

With `curl` or Postman:
```bash
POST http://localhost:8080/api/auth/register
Content-Type: application/json

{
  "username": "sarah",
  "password": "CashierPass456",
  "role": "cashier"
}
```

You'll need to be authenticated as admin. The example calls this via a frontend action, but it's worth knowing the raw endpoint.

## Common Gotchas

1. **"This site can't be reached"** — Backend isn't running. Check `docker compose logs backend`

2. **Can't log in** — Wrong bootstrap credentials or database didn't initialize. Kill everything and restart:
   ```bash
   docker compose down -v
   docker compose up --build
   ```
   The `-v` deletes the database volume so it reinitializes fresh.

3. **Nginx says "host not found in upstream backend"** — Backend crashed. Whatever caused it, fix the backend first.

4. **Session timeouts constantly** — Check `SESSION_TTL_MINUTES` in `.env`. Default is 480 (8 hours).

5. **Rate limiting too strict** — Adjust `LOGIN_MAX_ATTEMPTS` and `LOGIN_WINDOW_SECONDS` in `.env`. No need to restart, they reload on app boot.

## Checking Backend Health

```bash
curl http://localhost:8080/api/health
```

Should return `{"status": "healthy"}` with a 200 response.

## Stopping Everything

```bash
docker compose down
```

To also wipe the database (for fresh start):
```bash
docker compose down -v
```

## Database Details

The schema is in `docker/postgres/init/001_schema.sql`. It runs automatically when the db container first starts.

**Tables you'll care about**:
- `users` — login accounts
- `products` — inventory
- `sales` — transactions
- `audit_log` — security events

You can connect directly if needed:
```bash
docker exec -it pos_system_with_cpp-db-1 psql -U posapp -d posdb
```

## Production Checklist (When You Get There)

- [ ] Set `COOKIE_SECURE=true` (requires HTTPS)
- [ ] Remove bootstrap credentials from `.env` after first login
- [ ] Use a secrets manager, not plaintext `.env`
- [ ] Restrict `CORS_ORIGIN` to your actual domain
- [ ] Set up database backups
- [ ] Review the audit log table regularly

## What Happens Under the Hood (You Don't Need to Know, But...)

Passwords are hashed with PBKDF2-HMAC-SHA256 (configurable iterations, default 210,000). Session tokens are generated from a CSPRNG and stored as SHA-256 hashes in the database — even if someone steals the database, they can't forge sessions.

All SQL is parameterized to prevent injection. Stock decrements and sales are transactional.

That's the security story. You're good.

## Questions?

- Backend code: `backend/src/`
- Frontend code: `frontend/src/`
- Docker configs: `docker-compose.yml`, `backend/Dockerfile`, `frontend/Dockerfile`

Read the README.md for the full design philosophy.

---

**TL;DR**: Copy `.env.example` to `.env`, fix the password if it has `#`, run `docker compose up --build`, go to localhost:3000, log in with your bootstrap credentials, you're done.
