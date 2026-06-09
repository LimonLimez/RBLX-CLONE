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
- `GAME_WORLDS_DIR` - directory for published `.world` files.
- `GAME_SERVER_HOST` and `GAME_SERVER_BASE_PORT` - local host/port range for spawned game instances.
- `SERVER_EXECUTABLE_PATH` - `Server.exe` path used when a game page starts an instance.
- `CLIENT_EXECUTABLE_PATH` - `Client.exe` path used when the Play button launches the native player.
- `PUBLIC_BASE_URL` - site URL passed to native server/client for web auth.

## API

- `POST /api/signup` with `{ "username": "Player_1", "password": "long-password" }`.
- `POST /api/login` with `{ "username": "Player_1", "password": "long-password" }`.
- `POST /api/verify` with `Authorization: Bearer <token>`.
- `GET /api/avatar` with `Authorization: Bearer <token>`.
- `POST /api/avatar` with `Authorization: Bearer <token>` and `{ "avatar": { ...six RGB arrays..., "faceId": "wink" } }`.
- `GET /api/me/social` with `Authorization: Bearer <token>`.
- `POST /api/me/playtime` with `Authorization: Bearer <token>` and `{ "seconds": 120 }`.
- `GET /api/users/search?q=Player` with `Authorization: Bearer <token>`.
- `GET /api/users/:id` with `Authorization: Bearer <token>`.
- `POST /api/friends/request` with `Authorization: Bearer <token>` and `{ "userId": 2 }`.
- `POST /api/friends/respond` with `Authorization: Bearer <token>` and `{ "userId": 2, "action": "accept" }`.
- `POST /api/friends/remove` with `Authorization: Bearer <token>` and `{ "userId": 2 }`.
- `GET /api/games` lists public games.
- `GET /api/games/:id` returns game details, world stats, and active local instances.
- `GET /api/games/mine` with `Authorization: Bearer <token>` lists your games.
- `POST /api/games` with `Authorization: Bearer <token>` publishes a world file as a game.
- `POST /api/games/publish` creates or updates a game for Studio publishing.
- `POST /api/games/:id/play` starts or reuses a local game server and returns Player launch metadata.

Avatar tokens are not accepted in query strings. `/api/verify` has a deprecated body-token fallback for older callers.

Valid avatar `faceId` values are `classic`, `happy`, `surprised`, `smirk`, and `wink`.

The Windows game server uses `/api/me/playtime` to add authenticated session time during play and on disconnect.

The games page lives at `/games`, game detail pages live at `/games/:id`, and the browser publishing form lives at `/create`.
