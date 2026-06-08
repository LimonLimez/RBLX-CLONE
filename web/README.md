# RBLX Clone Web Backend

This is the local Express backend for account signup/login, token verification, and avatar storage.

## Setup

```powershell
npm.cmd install
npm.cmd test
npm.cmd start
```

Use `npm.cmd` on Windows PowerShell if script execution policy blocks `npm.ps1`.

Copy `.env.example` to `.env` for local overrides. Do not commit `.env`.

## Environment

- `NODE_ENV` - `development`, `test`, or `production`.
- `PORT` - server port, default `3000`.
- `DATABASE_PATH` - SQLite database path, default `users.db`.
- `JWT_SECRET` - required and strong in production.
- `JWT_EXPIRES_IN` - token lifetime, default `7d`.
- `CORS_ORIGIN` - comma-separated allowed origins.
- `JSON_BODY_LIMIT` - JSON body size limit.
- `AUTH_RATE_LIMIT_WINDOW_MS` and `AUTH_RATE_LIMIT_MAX` - auth endpoint rate limit.

## API

- `POST /api/signup` with `{ "username": "Player_1", "password": "long-password" }`.
- `POST /api/login` with `{ "username": "Player_1", "password": "long-password" }`.
- `POST /api/verify` with `Authorization: Bearer <token>`.
- `GET /api/avatar` with `Authorization: Bearer <token>`.
- `POST /api/avatar` with `Authorization: Bearer <token>` and `{ "avatar": { ... } }`.

Avatar tokens are not accepted in query strings. `/api/verify` has a deprecated body-token fallback for older callers.
