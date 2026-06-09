# RBLX Clone

RBLX Clone is an early prototype of a Roblox-style client, game server, Studio editor, and web authentication/avatar service. It is usable for local experimentation, but it is not production-ready.

## Current Status

- Client, Server, and Studio are C++17 targets.
- The Client and Server currently require Windows because they use WinSock and WinHTTP.
- Studio uses GLFW/OpenGL/ImGui/ImGuizmo and has a basic world editing path.
- The web backend is a Node.js/Express service with SQLite, bcrypt password hashing, JWT authentication, and avatar storage.
- Networking is still a compact binary protocol. It now has shared framing, payload limits, partial send handling, and receive buffering, but it is still a prototype protocol.

## Repository Layout

- `CMakeLists.txt` - C++ build configuration.
- `CMakePresets.json` - Visual Studio debug preset for Windows.
- `include/` - C++ headers.
- `src/` - C++ Client, Server, Studio, rendering, physics, auth, and world loading code.
- `shaders/` - OpenGL shader files used by Client and Studio.
- `tests/` - C++ protocol tests.
- `web/` - Express backend and public HTML pages.
- `ServerWorld.world` - Default world file.
- `GameRelease/` - Existing release asset folders. Do not commit generated binaries here.

## Prerequisites

### C++ Targets

Windows is currently required for Client and Server.

- Visual Studio 2022 with "Desktop development with C++".
- CMake 3.21 or newer if using `CMakePresets.json`; CMake 3.14 or newer for manual configure.
- OpenGL-capable GPU/drivers.

### Web Backend

- Node.js 20 or newer. The project was tested with Node.js 24.
- npm.

PowerShell may block `npm.ps1` on some machines. Use `npm.cmd` if that happens.

## Build C++

From a Developer PowerShell or Developer Command Prompt:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Manual configure also works:

```powershell
cmake -S . -B build/windows-debug -G "Visual Studio 17 2022" -DRBLX_BUILD_TESTS=ON
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug
```

The project has also been verified with a portable MinGW/Ninja toolchain:

```powershell
cmake -S . -B build/ninja -G Ninja -DCMAKE_C_COMPILER=C:\path\to\gcc.exe -DCMAKE_CXX_COMPILER=C:\path\to\g++.exe -DRBLX_BUILD_TESTS=ON
cmake --build build/ninja
ctest --test-dir build/ninja --output-on-failure
```

Useful CMake options:

- `RBLX_BUILD_CLIENT=ON|OFF`
- `RBLX_BUILD_SERVER=ON|OFF`
- `RBLX_BUILD_STUDIO=ON|OFF`
- `RBLX_BUILD_TESTS=ON|OFF`
- `RBLX_ENABLE_WARNINGS=ON|OFF`

On non-Windows hosts, CMake disables Client and Server because those targets depend on WinSock/WinHTTP.

## Run C++ Targets

After building Debug with the preset:

```powershell
.\build\windows-debug\Debug\Studio.exe
.\build\windows-debug\Debug\Server.exe
.\build\windows-debug\Debug\Client.exe
```

Start the web backend before using authenticated Client login.

## Web Backend Setup

```powershell
cd web
npm.cmd install
npm.cmd test
npm.cmd start
```

The backend listens on `http://localhost:3000` by default.

Copy `web/.env.example` to `web/.env` for local overrides. Do not commit `.env`.

Environment variables:

- `NODE_ENV` - `development`, `test`, or `production`.
- `PORT` - HTTP port, default `3000`.
- `DATABASE_PATH` - SQLite database path, default `web/users.db`.
- `JWT_SECRET` - required and must be strong in production.
- `JWT_EXPIRES_IN` - JWT lifetime, default `7d`.
- `CORS_ORIGIN` - comma-separated allowed origins. Development defaults to localhost origins.
- `JSON_BODY_LIMIT` - Express JSON body limit, default `16kb`.
- `AUTH_RATE_LIMIT_WINDOW_MS` - auth rate limit window.
- `AUTH_RATE_LIMIT_MAX` - auth requests allowed per window.

Production startup fails if `JWT_SECRET` is missing, too short, or left as an unsafe placeholder.

## Web API

Authentication:

- `POST /api/signup` with JSON `{ "username": "Player_1", "password": "long-password" }`
- `POST /api/login` with JSON `{ "username": "Player_1", "password": "long-password" }`
- `POST /api/verify` with `Authorization: Bearer <token>`

Avatar:

- `GET /api/avatar` with `Authorization: Bearer <token>`
- `POST /api/avatar` with `Authorization: Bearer <token>` and JSON `{ "avatar": { ...six RGB arrays... } }`

Profiles and friends:

- `GET /api/me/social` returns the signed-in profile, friends, and friend requests.
- `POST /api/me/playtime` with `{ "seconds": 120 }` records authenticated playtime.
- `GET /api/users/search?q=Player` searches users by username or exact user ID.
- `GET /api/users/:id` returns a public profile, avatar, stats, and friends.
- `POST /api/friends/request` with `{ "userId": 2 }` sends or accepts a friend request.
- `POST /api/friends/respond` with `{ "userId": 2, "action": "accept" }` accepts or declines an incoming request.
- `POST /api/friends/remove` with `{ "userId": 2 }` removes a friend or cancels a pending request.

Tokens are no longer accepted in avatar URLs. `/api/verify` still accepts a body token as a deprecated compatibility fallback, but new callers should use the Authorization header.

The game server reports authenticated player sessions to `/api/me/playtime` while a player is connected and once more when the player disconnects.

## Studio Controls

- Right mouse drag: look around.
- `W/A/S/D`: move camera.
- `E/Q`: move camera up/down.
- Shift: faster camera movement.
- Left click: select part.
- Toolbox: add parts, switch move/rotate/scale tools, enable snap.
- Properties: edit selected part fields.

Scale operations assign the decomposed absolute scale from ImGuizmo and clamp it to a safe positive range. They do not multiply scale every frame.

## Known Limitations

- This is a prototype and should be treated as local-only.
- Client and Server are Windows-only until socket and HTTP abstractions are made portable.
- The binary protocol still depends on matching C++ struct layouts across the same build family.
- The web service uses SQLite and local JWT storage in the browser; it is not hardened for internet deployment.
- Client UI/networking code remains large and should be split further.
- C++ build/test is verified with CMake, Ninja, and MinGW GCC. Client, Server, and Studio runtime behavior still need manual testing with a graphics-capable Windows desktop.

## Troubleshooting

- `cmake is not recognized`: install CMake or add it to PATH.
- `cl not found`: run from a Visual Studio Developer shell or install the C++ workload.
- `npm.ps1 cannot be loaded`: use `npm.cmd` from PowerShell.
- `JWT_SECRET must be set`: set a strong secret in production or use development mode locally.
- Client login fails: start the web backend first and confirm `http://localhost:3000` is reachable.
- Client cannot connect: start `Server.exe`, allow firewall access, and connect to `127.0.0.1` for local testing.
