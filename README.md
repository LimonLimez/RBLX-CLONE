# RBLX Clone Engine

A complete Roblox-like game engine built with C++ and modern web technologies. Features a multiplayer client-server architecture with web-based authentication, avatar customization, and a blocky character system reminiscent of classic Roblox (2009-2014 aesthetic).

## 🎮 Features

- **Multiplayer Client-Server Architecture**: Real-time multiplayer with authoritative server
- **Web Authentication**: User signup, login, and JWT token-based authentication
- **Avatar Customization**: Web-based avatar creator with color customization for all body parts
- **Blocky Characters**: Classic Roblox-style blocky characters with smooth animations
- **Physics Engine**: ReactPhysics3D for realistic physics simulation
- **3D Rendering**: OpenGL-based rendering with shadows, skybox, and modern graphics
- **World System**: Load and save game worlds with parts, physics, and spawn points
- **Player List**: See all connected players in real-time
- **Guest Mode**: Play without an account
- **Online/Offline Modes**: Play solo or connect to a server

## 📁 Project Structure

```
rblxgameengine/
├── include/                    # Header files
│   ├── Auth.h                  # Client-side HTTP authentication utilities
│   ├── Avatar.h                # Avatar data structures
│   ├── Camera.h                # 3D camera system
│   ├── Character.h             # Player character class
│   ├── Editor.h                # World editor (Studio) interface
│   ├── Network.h               # Network packet definitions
│   ├── Part.h                  # Game part/world object structure
│   ├── PhysicsWorld.h          # Physics world management
│   ├── Raycaster.h             # 3D raycasting for selection
│   ├── Renderer.h              # OpenGL rendering system
│   ├── ServerAuth.h            # Server-side HTTP authentication
│   ├── Shader.h                # GLSL shader management
│   ├── ShadowMap.h             # Shadow mapping system
│   ├── Skybox.h                # Skybox rendering
│   ├── Window.h                # GLFW window management
│   └── WorldLoader.h           # World file loading/saving
│
├── src/                        # Source files
│   ├── Avatar.cpp              # Avatar implementation
│   ├── Camera.cpp              # Camera controls and transformations
│   ├── Character.cpp           # Character physics, animation, and movement
│   ├── Client.cpp              # Main client application (game client)
│   ├── Editor.cpp              # World editor implementation
│   ├── main.cpp                # Studio application entry point
│   ├── PhysicsWorld.cpp        # Physics world setup and management
│   ├── Player.cpp              # Player state management
│   ├── Raycaster.cpp           # 3D raycasting implementation
│   ├── Renderer.cpp            # OpenGL rendering pipeline
│   ├── Server.cpp              # Game server application
│   ├── ServerAuth.cpp          # Server HTTP authentication utilities
│   ├── Shader.cpp              # Shader compilation and management
│   ├── ShadowMap.cpp           # Shadow rendering
│   ├── Skybox.cpp              # Skybox rendering
│   ├── Window.cpp              # Window creation and event handling
│   └── WorldLoader.cpp         # World file I/O
│
├── shaders/                    # GLSL shader files
│   ├── vertex.glsl             # Vertex shader
│   ├── fragment.glsl           # Fragment shader
│   ├── shadow_depth_vertex.glsl    # Shadow depth vertex shader
│   ├── shadow_depth_fragment.glsl  # Shadow depth fragment shader
│   ├── skybox_vertex.glsl      # Skybox vertex shader
│   └── skybox_fragment.glsl    # Skybox fragment shader
│
├── web/                        # Web server (Node.js/Express)
│   ├── server.js               # Express server with authentication API
│   ├── package.json            # Node.js dependencies
│   ├── users.db                # SQLite database (auto-created)
│   └── public/                 # Static HTML pages
│       ├── index.html          # Home page
│       ├── login.html          # Login page
│       ├── signup.html         # Sign up page
│       ├── dashboard.html      # User dashboard
│       └── avatar.html         # Avatar customization page
│
├── GameRelease/                # Release builds and assets
│   ├── Client/                 # Client release files
│   │   ├── Client.exe          # Compiled client executable
│   │   ├── shaders/            # Shader files (copied for runtime)
│   │   └── *.txt               # Configuration/log files
│   └── Server/                 # Server release files
│       ├── Server.exe          # Compiled server executable
│       └── ServerWorld.world   # Default server world file
│
├── CMakeLists.txt              # CMake build configuration
├── .gitignore                  # Git ignore rules
└── README.md                   # This file
```

## 🛠️ Prerequisites

### For C++ Build (Client/Server/Studio)

1. **CMake** (3.14 or higher)
   - Download: https://cmake.org/download/
   - Add to PATH during installation

2. **C++ Compiler**
   - **Windows**: Visual Studio 2022 Community (or later)
     - Install "Desktop development with C++" workload
     - Download: https://visualstudio.microsoft.com/vs/community/
   - **Linux**: `g++` or `clang++`
     ```bash
     sudo apt-get install build-essential cmake
     ```
   - **macOS**: Xcode Command Line Tools
     ```bash
     xcode-select --install
     ```

### For Web Server

1. **Node.js** (16.0 or higher)
   - Download: https://nodejs.org/
   - Includes `npm` package manager

## 🔨 Building the Project

### Step 1: Clone the Repository

```bash
git clone https://github.com/Limezzzzzz123/RBLX-CLONE.git
cd RBLX-CLONE
```

### Step 2: Build C++ Applications

#### Windows (PowerShell)

```powershell
# Create build directory
mkdir build2
cd build2

# Configure project (downloads dependencies automatically)
cmake ..

# Build all targets (Client, Server, Studio)
cmake --build . --config Release

# Or build specific targets:
cmake --build . --config Release --target Client
cmake --build . --config Release --target Server
cmake --build . --config Release --target Studio
```

#### Linux/macOS

```bash
# Create build directory
mkdir build
cd build

# Configure project
cmake ..

# Build all targets
cmake --build . --config Release

# Or build specific targets:
cmake --build . --config Release --target Client
cmake --build . --config Release --target Server
cmake --build . --config Release --target Studio
```

**Note**: CMake will automatically download and build all dependencies:
- GLFW (Window management)
- GLM (Math library)
- Dear ImGui (UI library)
- ImGuizmo (3D gizmos for Studio)
- ReactPhysics3D (Physics engine)
- STB (Image loading)
- GLAD (OpenGL loader)

### Step 3: Build Web Server

```bash
cd web
npm install
```

This installs:
- Express (Web framework)
- SQLite3 (Database)
- Bcrypt (Password hashing)
- JSON Web Token (JWT authentication)
- CORS (Cross-origin resource sharing)

## 🚀 Running the Project

### 1. Start the Web Server (Required for Authentication)

```bash
cd web
npm start
```

The server will start on `http://localhost:3000`

**First Run**: The server automatically creates `users.db` SQLite database.

### 2. Start the Game Server

#### Windows
```powershell
cd build2\Release
.\Server.exe
```

#### Linux/macOS
```bash
cd build/Release
./Server
```

The server will:
- Listen on port `7777` (default)
- Load `ServerWorld.world` from the server directory
- Accept client connections
- Handle player synchronization and physics

### 3. Start the Game Client

#### Windows
```powershell
cd build2\Release
.\Client.exe
```

#### Linux/macOS
```bash
cd build/Release
./Client
```

**Client Features**:
- **Login Menu**: Choose to Login, Sign Up, or Play as Guest
- **Play Options**: Choose Online (connect to server) or Offline (solo play)
- **Server Connection**: Enter server IP (default: `127.0.0.1`) to connect
- **Player List**: View all connected players
- **Avatar Customization**: Opens web browser to customize avatar

### 4. Start Studio (World Editor)

#### Windows
```powershell
cd build2\Release
.\Studio.exe
```

#### Linux/macOS
```bash
cd build/Release
./Studio
```

**Note**: Studio is currently in development. The preview shows a blue screen as it's not yet fully implemented.

## 🎮 Controls

### Client (In-Game)

- **W/A/S/D**: Move character
- **Space**: Jump
- **Mouse**: Look around (camera follows character)
- **Arrow Up/Down**: Zoom camera in/out
- **ESC**: Open menu (disconnect, exit)

### Studio (Editor - Coming Soon)

- **W/A/S/D**: Move camera
- **Right Click + Drag**: Rotate camera
- **E**: Move camera up
- **Q**: Move camera down
- **Left Click**: Select parts
- **Gizmos**: Move/rotate/scale selected parts (ImGuizmo)

## 🌐 Web Interface

### Pages

- **`http://localhost:3000/`**: Home page with links
- **`http://localhost:3000/login`**: User login
- **`http://localhost:3000/signup`**: Create new account
- **`http://localhost:3000/dashboard`**: User dashboard (after login)
- **`http://localhost:3000/avatar`**: Avatar customization tool

### API Endpoints

#### Authentication

**POST `/api/signup`**
```json
{
  "username": "string",
  "password": "string"
}
```
Response:
```json
{
  "success": true,
  "token": "jwt_token_here",
  "userId": 1,
  "username": "string"
}
```

**POST `/api/login`**
```json
{
  "username": "string",
  "password": "string"
}
```
Response: Same as signup

**POST `/api/verify`**
```json
{
  "token": "jwt_token_here"
}
```
Response:
```json
{
  "success": true,
  "userId": 1,
  "username": "string"
}
```

#### Avatar

**GET `/api/avatar`**
- Headers: `Authorization: Bearer <token>`
- Returns: Avatar color data for authenticated user

**POST `/api/avatar`**
- Headers: `Authorization: Bearer <token>`
- Body:
```json
{
  "headColor": [r, g, b],
  "torsoColor": [r, g, b],
  "leftArmColor": [r, g, b],
  "rightArmColor": [r, g, b],
  "leftLegColor": [r, g, b],
  "rightLegColor": [r, g, b]
}
```

## 🏗️ Architecture

### Client-Server Communication

The client and server communicate using custom binary packets:

- **Packet Types**:
  - `CONNECT`: Client connects with authentication token
  - `PLAYER_STATE`: Client sends position/rotation updates
  - `PLAYER_JOIN`: Server notifies of new player
  - `PLAYER_LEAVE`: Server notifies of player disconnect
  - `PLAYER_LIST`: Server sends list of all players with avatars
  - `WORLD_STATE`: Server sends complete world state to new clients
  - `PART_UPDATE`: Server sends part position/rotation updates

### Physics

- **Client (Online)**: Server-authoritative physics
  - World parts are kinematic (server-controlled)
  - Local character is kinematic (client-controlled, server-validated)
  - Remote characters are kinematic (server-controlled)

- **Client (Offline)**: Full physics simulation
  - All parts are dynamic (physics-controlled)
  - Character is kinematic (player-controlled)

- **Server**: Full physics simulation
  - All world parts are dynamic
  - Player characters are kinematic (server-controlled)

### Avatar System

1. User customizes avatar on web interface (`/avatar`)
2. Avatar colors saved to database via `/api/avatar`
3. Client fetches avatar on login via `/api/avatar`
4. Server fetches avatar on player connect
5. Server broadcasts avatar data in `PLAYER_LIST` packets
6. Clients apply avatar colors to character parts

## 🔧 Configuration

### Server Configuration

Edit `ServerWorld.world` to modify the default world:
- Part definitions (position, size, color, physics properties)
- Spawn points (`isSpawn: true`)

### Client Configuration

- Server IP: Enter in client UI (default: `127.0.0.1`)
- Auth token: Saved in `auth_token.txt` (auto-created)

### Web Server Configuration

Edit `web/server.js`:
- **Port**: Change `const PORT = 3000;`
- **JWT Secret**: **IMPORTANT** - Change `JWT_SECRET` before production deployment!

## 🐛 Troubleshooting

### Build Issues

**CMake not found**:
- Ensure CMake is installed and in PATH
- Restart terminal after installation

**Visual Studio not found**:
- Install Visual Studio 2022 with C++ workload
- Run CMake from "Developer Command Prompt for VS"

**Dependencies fail to download**:
- Check internet connection
- CMake downloads dependencies during configuration

### Runtime Issues

**Client can't connect to server**:
- Ensure server is running
- Check firewall settings
- Verify server IP address

**Web server errors**:
- Ensure Node.js is installed: `node --version`
- Reinstall dependencies: `cd web && rm -rf node_modules && npm install`
- Check if port 3000 is already in use

**Database errors**:
- Delete `web/users.db` to reset database
- Ensure write permissions in `web/` directory

## 📝 Development Notes

### Adding New Features

- **New Packet Types**: Add to `include/Network.h`
- **New UI Elements**: Modify `src/Client.cpp` (ImGui code)
- **New API Endpoints**: Add to `web/server.js`
- **New World Objects**: Extend `Part` structure in `include/Part.h`

### Code Style

- C++17 standard
- Header files in `include/`
- Source files in `src/`
- Use consistent naming conventions
- Comment complex logic

## 📄 License

This project is open source. See repository for license details.

## 🤝 Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## 📧 Contact

- GitHub: [Limezzzzzz123](https://github.com/Limezzzzzz123)
- Repository: https://github.com/Limezzzzzz123/RBLX-CLONE

---

**Enjoy building and playing! 🎮**
