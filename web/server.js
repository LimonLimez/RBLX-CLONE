const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');
const cors = require('cors');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const fs = require('fs');
const path = require('path');
const net = require('net');
const { spawn } = require('child_process');
const crypto = require('crypto');

const DEFAULT_AVATAR = Object.freeze({
    headColor: [0.8, 0.6, 0.4],
    torsoColor: [0.2, 0.4, 0.8],
    leftArmColor: [0.8, 0.6, 0.4],
    rightArmColor: [0.8, 0.6, 0.4],
    leftLegColor: [0.2, 0.6, 0.2],
    rightLegColor: [0.2, 0.6, 0.2]
});

const DEFAULT_FACE_ID = 'classic';
const FACE_IDS = new Set(['classic', 'happy', 'surprised', 'smirk', 'wink']);
const AVATAR_FIELDS = Object.keys(DEFAULT_AVATAR);
const USERNAME_PATTERN = /^[A-Za-z0-9_]{3,20}$/;
const DEFAULT_GAME_ICON = '/assets/games/default-game-icon.png';
const DEFAULT_GAME_BANNER = '/assets/games/default-game-banner.png';
const MAX_WORLD_BYTES = 2 * 1024 * 1024;
const UNSAFE_DEV_SECRETS = new Set([
    '',
    'your-secret-key-change-this-in-production',
    'replace-this-with-a-long-random-secret-before-production',
    'change-me',
    'dev-secret',
    'local-dev-secret',
    'local-dev-secret-change-me'
]);

function parseInteger(value, fallback) {
    const parsed = Number.parseInt(value, 10);
    return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function splitCsv(value) {
    if (!value) return [];
    return value.split(',').map((item) => item.trim()).filter(Boolean);
}

function resolveConfiguredPath(value, fallback) {
    if (!value) return fallback;
    return path.isAbsolute(value) ? value : path.resolve(__dirname, value);
}

function firstExistingPath(candidates) {
    return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || '';
}

function loadConfig(env = process.env) {
    const nodeEnv = env.NODE_ENV || 'development';
    const isProduction = nodeEnv === 'production';
    const jwtSecret = env.JWT_SECRET || (isProduction ? '' : 'local-dev-secret-change-me');

    if (isProduction && (UNSAFE_DEV_SECRETS.has(jwtSecret) || jwtSecret.length < 32)) {
        throw new Error('JWT_SECRET must be set to a strong non-default value in production.');
    }

    const defaultCorsOrigins = isProduction
        ? []
        : ['http://localhost:3000', 'http://127.0.0.1:3000'];
    const repoRoot = path.resolve(__dirname, '..');
    const port = parseInteger(env.PORT, 3000);
    const serverExecutablePath = resolveConfiguredPath(env.SERVER_EXECUTABLE_PATH, firstExistingPath([
        path.join(repoRoot, 'build-portable4', 'Server.exe'),
        path.join(repoRoot, 'build-portable3', 'Server.exe'),
        path.join(repoRoot, 'build', 'windows-debug', 'Debug', 'Server.exe'),
        path.join(repoRoot, 'GameRelease', 'Server', 'Server.exe')
    ]));
    const clientExecutablePath = resolveConfiguredPath(env.CLIENT_EXECUTABLE_PATH, firstExistingPath([
        path.join(repoRoot, 'build-portable4', 'Client.exe'),
        path.join(repoRoot, 'build-portable3', 'Client.exe'),
        path.join(repoRoot, 'build', 'windows-debug', 'Debug', 'Client.exe'),
        path.join(repoRoot, 'GameRelease', 'Client', 'Client.exe')
    ]));

    return {
        nodeEnv,
        isProduction,
        port,
        jwtSecret,
        jwtExpiresIn: env.JWT_EXPIRES_IN || '7d',
        databasePath: env.DATABASE_PATH || path.join(__dirname, 'users.db'),
        corsOrigins: splitCsv(env.CORS_ORIGIN).length > 0 ? splitCsv(env.CORS_ORIGIN) : defaultCorsOrigins,
        bcryptRounds: parseInteger(env.BCRYPT_ROUNDS, nodeEnv === 'test' ? 4 : 10),
        authRateLimitWindowMs: parseInteger(env.AUTH_RATE_LIMIT_WINDOW_MS, 15 * 60 * 1000),
        authRateLimitMax: parseInteger(env.AUTH_RATE_LIMIT_MAX, nodeEnv === 'test' ? 1000 : 20),
        jsonBodyLimit: env.JSON_BODY_LIMIT || '2mb',
        repoRoot,
        gameWorldsDir: resolveConfiguredPath(env.GAME_WORLDS_DIR, path.join(__dirname, 'game-worlds')),
        gameServerHost: env.GAME_SERVER_HOST || '127.0.0.1',
        gameServerBasePort: parseInteger(env.GAME_SERVER_BASE_PORT, 7777),
        gameInstanceEmptyGraceMs: parseInteger(env.GAME_INSTANCE_EMPTY_GRACE_MS, 30 * 1000),
        publicBaseUrl: env.PUBLIC_BASE_URL || `http://localhost:${port}`,
        serverExecutablePath,
        clientExecutablePath
    };
}

function openDatabase(databasePath) {
    if (databasePath !== ':memory:') {
        fs.mkdirSync(path.dirname(databasePath), { recursive: true });
    }
    return new sqlite3.Database(databasePath);
}

function dbRun(db, sql, params = []) {
    return new Promise((resolve, reject) => {
        db.run(sql, params, function onRun(err) {
            if (err) {
                reject(err);
                return;
            }
            resolve({ lastID: this.lastID, changes: this.changes });
        });
    });
}

function dbGet(db, sql, params = []) {
    return new Promise((resolve, reject) => {
        db.get(sql, params, (err, row) => {
            if (err) {
                reject(err);
                return;
            }
            resolve(row);
        });
    });
}

function dbAll(db, sql, params = []) {
    return new Promise((resolve, reject) => {
        db.all(sql, params, (err, rows) => {
            if (err) {
                reject(err);
                return;
            }
            resolve(rows);
        });
    });
}

async function initializeDatabase(db) {
    await dbRun(db, `CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT UNIQUE NOT NULL,
        password_hash TEXT NOT NULL,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )`);

    await dbRun(db, `CREATE TABLE IF NOT EXISTS avatars (
        user_id INTEGER PRIMARY KEY,
        head_color_r REAL DEFAULT 0.8,
        head_color_g REAL DEFAULT 0.6,
        head_color_b REAL DEFAULT 0.4,
        torso_color_r REAL DEFAULT 0.2,
        torso_color_g REAL DEFAULT 0.4,
        torso_color_b REAL DEFAULT 0.8,
        left_arm_color_r REAL DEFAULT 0.8,
        left_arm_color_g REAL DEFAULT 0.6,
        left_arm_color_b REAL DEFAULT 0.4,
        right_arm_color_r REAL DEFAULT 0.8,
        right_arm_color_g REAL DEFAULT 0.6,
        right_arm_color_b REAL DEFAULT 0.4,
        left_leg_color_r REAL DEFAULT 0.2,
        left_leg_color_g REAL DEFAULT 0.6,
        left_leg_color_b REAL DEFAULT 0.2,
        right_leg_color_r REAL DEFAULT 0.2,
        right_leg_color_g REAL DEFAULT 0.6,
        right_leg_color_b REAL DEFAULT 0.2,
        face_id TEXT DEFAULT 'classic',
        FOREIGN KEY (user_id) REFERENCES users(id)
    )`);

    const avatarColumns = await dbAll(db, 'PRAGMA table_info(avatars)');
    if (!avatarColumns.some((column) => column.name === 'face_id')) {
        await dbRun(db, "ALTER TABLE avatars ADD COLUMN face_id TEXT DEFAULT 'classic'");
    }

    await dbRun(db, `CREATE TABLE IF NOT EXISTS friendships (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        requester_id INTEGER NOT NULL,
        addressee_id INTEGER NOT NULL,
        status TEXT NOT NULL CHECK(status IN ('pending', 'accepted')),
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        UNIQUE(requester_id, addressee_id),
        CHECK(requester_id <> addressee_id),
        FOREIGN KEY (requester_id) REFERENCES users(id),
        FOREIGN KEY (addressee_id) REFERENCES users(id)
    )`);

    await dbRun(db, `CREATE TABLE IF NOT EXISTS user_stats (
        user_id INTEGER PRIMARY KEY,
        playtime_seconds INTEGER NOT NULL DEFAULT 0,
        last_played_at DATETIME,
        updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (user_id) REFERENCES users(id)
    )`);

    await dbRun(db, `CREATE TABLE IF NOT EXISTS games (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        owner_id INTEGER NOT NULL,
        title TEXT NOT NULL,
        slug TEXT NOT NULL,
        description TEXT NOT NULL DEFAULT '',
        is_public INTEGER NOT NULL DEFAULT 1,
        world_path TEXT NOT NULL,
        icon_path TEXT NOT NULL DEFAULT '${DEFAULT_GAME_ICON}',
        banner_path TEXT NOT NULL DEFAULT '${DEFAULT_GAME_BANNER}',
        parts_count INTEGER NOT NULL DEFAULT 0,
        dynamic_parts_count INTEGER NOT NULL DEFAULT 0,
        spawn_count INTEGER NOT NULL DEFAULT 0,
        launch_count INTEGER NOT NULL DEFAULT 0,
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        published_at DATETIME DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY (owner_id) REFERENCES users(id)
    )`);
}

function validateCredentials(username, password) {
    if (typeof username !== 'string' || typeof password !== 'string') {
        return 'Username and password are required.';
    }

    if (!USERNAME_PATTERN.test(username)) {
        return 'Username must be 3-20 characters and use only letters, numbers, or underscores.';
    }

    if (password.length < 8 || password.length > 128) {
        return 'Password must be between 8 and 128 characters.';
    }

    return null;
}

function validateLoginInput(username, password) {
    if (typeof username !== 'string' || typeof password !== 'string') {
        return 'Username and password are required.';
    }

    if (username.length > 20 || password.length > 128) {
        return 'Invalid username or password.';
    }

    return null;
}

function sanitizeAvatar(avatar) {
    if (!avatar || typeof avatar !== 'object' || Array.isArray(avatar)) {
        return null;
    }

    const sanitized = {};
    for (const field of AVATAR_FIELDS) {
        const value = avatar[field];
        if (!Array.isArray(value) || value.length !== 3) {
            return null;
        }

        sanitized[field] = value.map((component) => {
            const number = Number(component);
            if (!Number.isFinite(number) || number < 0 || number > 1) {
                return null;
            }
            return number;
        });

        if (sanitized[field].includes(null)) {
            return null;
        }
    }

    if (avatar.faceId !== undefined && !FACE_IDS.has(avatar.faceId)) {
        return null;
    }
    sanitized.faceId = avatar.faceId || DEFAULT_FACE_ID;

    return sanitized;
}

function normalizeFaceId(value) {
    return FACE_IDS.has(value) ? value : DEFAULT_FACE_ID;
}

function rowToAvatar(row) {
    if (!row) {
        return { ...DEFAULT_AVATAR, faceId: DEFAULT_FACE_ID };
    }

    return {
        headColor: [row.head_color_r, row.head_color_g, row.head_color_b],
        torsoColor: [row.torso_color_r, row.torso_color_g, row.torso_color_b],
        leftArmColor: [row.left_arm_color_r, row.left_arm_color_g, row.left_arm_color_b],
        rightArmColor: [row.right_arm_color_r, row.right_arm_color_g, row.right_arm_color_b],
        leftLegColor: [row.left_leg_color_r, row.left_leg_color_g, row.left_leg_color_b],
        rightLegColor: [row.right_leg_color_r, row.right_leg_color_g, row.right_leg_color_b],
        faceId: normalizeFaceId(row.face_id)
    };
}

function parseUserId(value) {
    const userId = Number.parseInt(value, 10);
    return Number.isInteger(userId) && userId > 0 ? userId : 0;
}

function clampPlaytimeSeconds(value) {
    const seconds = Number.parseInt(value, 10);
    if (!Number.isFinite(seconds) || seconds <= 0) return 0;
    return Math.min(seconds, 24 * 60 * 60);
}

function parsePlayerCount(value) {
    const count = Number.parseInt(value, 10);
    return Number.isInteger(count) && count >= 0 && count <= 200 ? count : -1;
}

function escapeLike(value) {
    return value.replace(/[\\%_]/g, (match) => `\\${match}`);
}

function toBoolean(value, fallback = true) {
    if (typeof value === 'boolean') return value;
    if (typeof value === 'number') return value !== 0;
    if (typeof value === 'string') {
        const normalized = value.trim().toLowerCase();
        if (['true', '1', 'yes', 'public'].includes(normalized)) return true;
        if (['false', '0', 'no', 'private'].includes(normalized)) return false;
    }
    return fallback;
}

function normalizeGameTitle(value) {
    const title = String(value || '').trim().replace(/\s+/g, ' ');
    if (title.length < 3 || title.length > 60) {
        return null;
    }
    return title;
}

function normalizeGameDescription(value) {
    return String(value || '').trim().replace(/\s+/g, ' ').slice(0, 240);
}

function slugify(value) {
    const slug = String(value || '')
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-+|-+$/g, '')
        .slice(0, 48);
    return slug || 'game';
}

function tokenizeWorldLine(line) {
    const tokens = [];
    const pattern = /"((?:\\"|[^"])*)"|(\S+)/g;
    let match;
    while ((match = pattern.exec(line)) !== null) {
        tokens.push(match[1] !== undefined ? match[1].replace(/\\"/g, '"') : match[2]);
    }
    return tokens;
}

function worldBool(value) {
    return value === '1' || value === 'true';
}

function analyzeWorldSource(worldText) {
    if (typeof worldText !== 'string' || worldText.trim().length === 0) {
        throw new Error('World data is required.');
    }

    const byteLength = Buffer.byteLength(worldText, 'utf8');
    if (byteLength > MAX_WORLD_BYTES) {
        throw new Error('World data is too large for this local server.');
    }

    const normalized = worldText.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    const lines = normalized.split('\n').filter((line) => line.trim().length > 0);
    const partsCount = Number.parseInt(lines[0], 10);
    if (!Number.isInteger(partsCount) || partsCount < 0 || partsCount > 4096) {
        throw new Error('World file header must contain a valid part count.');
    }

    if (lines.length - 1 < partsCount) {
        throw new Error('World file ended before all parts were present.');
    }

    let dynamicPartsCount = 0;
    let spawnCount = 0;
    const shapeCounts = { cube: 0, sphere: 0, wedge: 0, cylinder: 0 };

    for (let index = 0; index < partsCount; index += 1) {
        const tokens = tokenizeWorldLine(lines[index + 1]);
        if (tokens.length < 23) {
            throw new Error(`World part ${index + 1} is missing fields.`);
        }

        const shape = Number.parseInt(tokens[0], 10);
        if (!Number.isInteger(shape) || shape < 0 || shape > 3) {
            throw new Error(`World part ${index + 1} has an unsupported shape.`);
        }

        const numberFields = [2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22];
        for (const fieldIndex of numberFields) {
            const value = Number(tokens[fieldIndex]);
            if (!Number.isFinite(value)) {
                throw new Error(`World part ${index + 1} contains invalid numeric data.`);
            }
        }

        if (worldBool(tokens[5])) spawnCount += 1;
        if (!worldBool(tokens[20]) && !worldBool(tokens[3])) dynamicPartsCount += 1;

        const shapeName = ['cube', 'sphere', 'wedge', 'cylinder'][shape];
        shapeCounts[shapeName] += 1;
    }

    return {
        source: normalized.endsWith('\n') ? normalized : `${normalized}\n`,
        partsCount,
        dynamicPartsCount,
        spawnCount,
        shapeCounts
    };
}

function saveWorldFile(config, gameId, worldSource) {
    fs.mkdirSync(config.gameWorldsDir, { recursive: true });
    const filename = `game-${gameId}-${Date.now()}.world`;
    const worldPath = path.join(config.gameWorldsDir, filename);
    fs.writeFileSync(worldPath, worldSource, 'utf8');
    return worldPath;
}

function gameQueryBase() {
    return `SELECT g.id, g.owner_id, g.title, g.slug, g.description, g.is_public,
            g.world_path, g.icon_path, g.banner_path, g.parts_count, g.dynamic_parts_count,
            g.spawn_count, g.launch_count, g.created_at, g.updated_at, g.published_at,
            u.username AS owner_username
        FROM games g
        JOIN users u ON u.id = g.owner_id`;
}

function serializeGame(row, includeOwnerControls = false) {
    return {
        id: row.id,
        ownerId: row.owner_id,
        ownerUsername: row.owner_username,
        title: row.title,
        slug: row.slug,
        description: row.description || '',
        isPublic: Boolean(row.is_public),
        iconUrl: row.icon_path || DEFAULT_GAME_ICON,
        bannerUrl: row.banner_path || DEFAULT_GAME_BANNER,
        stats: {
            partsCount: Number(row.parts_count || 0),
            dynamicPartsCount: Number(row.dynamic_parts_count || 0),
            spawnCount: Number(row.spawn_count || 0),
            launchCount: Number(row.launch_count || 0)
        },
        createdAt: row.created_at,
        updatedAt: row.updated_at,
        publishedAt: row.published_at,
        canEdit: includeOwnerControls
    };
}

async function getGameRow(db, gameId) {
    return dbGet(db, `${gameQueryBase()} WHERE g.id = ?`, [gameId]);
}

async function createGameRecord(db, config, user, input) {
    const title = normalizeGameTitle(input && input.title);
    if (!title) {
        const error = new Error('Game title must be 3-60 characters.');
        error.statusCode = 400;
        throw error;
    }

    let analysis;
    try {
        analysis = analyzeWorldSource(String(input.worldText || input.world || ''));
    } catch (error) {
        error.statusCode = 400;
        throw error;
    }

    const result = await dbRun(db, `INSERT INTO games (
            owner_id, title, slug, description, is_public, world_path,
            icon_path, banner_path, parts_count, dynamic_parts_count, spawn_count
        ) VALUES (?, ?, ?, ?, ?, '', ?, ?, ?, ?, ?)`, [
        user.id,
        title,
        slugify(title),
        normalizeGameDescription(input.description),
        toBoolean(input.isPublic, true) ? 1 : 0,
        input.iconUrl || DEFAULT_GAME_ICON,
        input.bannerUrl || DEFAULT_GAME_BANNER,
        analysis.partsCount,
        analysis.dynamicPartsCount,
        analysis.spawnCount
    ]);

    const worldPath = saveWorldFile(config, result.lastID, analysis.source);
    await dbRun(db, `UPDATE games
        SET world_path = ?, updated_at = CURRENT_TIMESTAMP, published_at = CURRENT_TIMESTAMP
        WHERE id = ?`, [worldPath, result.lastID]);

    return getGameRow(db, result.lastID);
}

async function updateGameRecord(db, config, gameId, user, input) {
    const existing = await getGameRow(db, gameId);
    if (!existing || existing.owner_id !== user.id) {
        const error = new Error('Game not found.');
        error.statusCode = 404;
        throw error;
    }

    const title = input.title === undefined ? existing.title : normalizeGameTitle(input.title);
    if (!title) {
        const error = new Error('Game title must be 3-60 characters.');
        error.statusCode = 400;
        throw error;
    }

    let worldPath = existing.world_path;
    let partsCount = existing.parts_count;
    let dynamicPartsCount = existing.dynamic_parts_count;
    let spawnCount = existing.spawn_count;

    if (input.worldText !== undefined || input.world !== undefined) {
        let analysis;
        try {
            analysis = analyzeWorldSource(String(input.worldText || input.world || ''));
        } catch (error) {
            error.statusCode = 400;
            throw error;
        }
        worldPath = saveWorldFile(config, gameId, analysis.source);
        partsCount = analysis.partsCount;
        dynamicPartsCount = analysis.dynamicPartsCount;
        spawnCount = analysis.spawnCount;
    }

    await dbRun(db, `UPDATE games
        SET title = ?, slug = ?, description = ?, is_public = ?, world_path = ?,
            icon_path = ?, banner_path = ?, parts_count = ?, dynamic_parts_count = ?,
            spawn_count = ?, updated_at = CURRENT_TIMESTAMP, published_at = CURRENT_TIMESTAMP
        WHERE id = ?`, [
        title,
        slugify(title),
        input.description === undefined ? existing.description : normalizeGameDescription(input.description),
        toBoolean(input.isPublic, Boolean(existing.is_public)) ? 1 : 0,
        worldPath,
        input.iconUrl || existing.icon_path || DEFAULT_GAME_ICON,
        input.bannerUrl || existing.banner_path || DEFAULT_GAME_BANNER,
        partsCount,
        dynamicPartsCount,
        spawnCount,
        gameId
    ]);

    return getGameRow(db, gameId);
}

async function seedStarterGame(db, config) {
    const row = await dbGet(db, 'SELECT COUNT(*) AS count FROM games');
    if (Number(row && row.count) > 0) return;

    const owner = await dbGet(db, 'SELECT id, username FROM users ORDER BY id LIMIT 1');
    let ownerId = owner && owner.id;
    if (!ownerId) {
        const passwordHash = await bcrypt.hash('local-starter-account', config.bcryptRounds);
        const result = await dbRun(db, 'INSERT INTO users (username, password_hash) VALUES (?, ?)', [
            'RBLXCreator',
            passwordHash
        ]);
        ownerId = result.lastID;
    }

    const repoRoot = config.repoRoot || path.resolve(__dirname, '..');
    const starterGames = [
        {
            worldPath: path.join(repoRoot, 'ServerWorld.world'),
            title: 'Starter Baseplate',
            description: 'The default shared world, now published as the first public game.'
        },
        {
            worldPath: path.join(__dirname, 'seed-worlds', 'separate-server-test.world'),
            title: 'Separate Server Test Arena',
            description: 'A second published world for testing that each game starts its own local server instance.'
        }
    ];

    for (const seed of starterGames) {
        if (!fs.existsSync(seed.worldPath)) continue;

        const worldText = fs.readFileSync(seed.worldPath, 'utf8');
        await createGameRecord(db, config, { id: ownerId }, {
            title: seed.title,
            description: seed.description,
            isPublic: true,
            worldText
        });
    }
}

function isChildRunning(child) {
    return child && child.exitCode === null && !child.killed;
}

function clearEmptyShutdown(instance) {
    if (instance.shutdownTimer) {
        clearTimeout(instance.shutdownTimer);
        instance.shutdownTimer = null;
    }
    instance.emptyShutdownAt = null;
}

function stopGameInstance(app, instance, reason = 'idle') {
    if (!instance || !app.locals.gameInstances.has(instance.id)) return false;

    clearEmptyShutdown(instance);
    instance.status = 'stopping';
    instance.stopReason = reason;

    if (isChildRunning(instance.child)) {
        try {
            instance.child.kill();
        } catch (error) {
            if (app.locals.config.nodeEnv !== 'test') {
                console.error(`Could not stop game instance ${instance.id}:`, error.message);
            }
        }
    }

    app.locals.gameInstances.delete(instance.id);
    return true;
}

function scheduleEmptyShutdown(app, instance) {
    if (!instance || Number(instance.playerCount || 0) > 0) {
        if (instance) clearEmptyShutdown(instance);
        return;
    }

    const config = app.locals.config;
    const now = Date.now();
    const emptySinceMs = instance.emptySince ? Date.parse(instance.emptySince) : now;
    const startedEmptyAt = Number.isFinite(emptySinceMs) ? emptySinceMs : now;
    const delayMs = Math.max(0, config.gameInstanceEmptyGraceMs - (now - startedEmptyAt));

    clearEmptyShutdown(instance);
    instance.emptySince = new Date(startedEmptyAt).toISOString();
    instance.emptyShutdownAt = new Date(now + delayMs).toISOString();
    instance.shutdownTimer = setTimeout(() => {
        const current = app.locals.gameInstances.get(instance.id);
        if (!current || Number(current.playerCount || 0) > 0) return;
        stopGameInstance(app, current, 'empty');
    }, delayMs);
    if (typeof instance.shutdownTimer.unref === 'function') {
        instance.shutdownTimer.unref();
    }
}

async function isPortAvailable(host, port) {
    return new Promise((resolve) => {
        const tester = net.createServer()
            .once('error', () => resolve(false))
            .once('listening', () => {
                tester.close(() => resolve(true));
            })
            .listen(port, host);
    });
}

async function findAvailablePort(host, startPort, usedPorts) {
    for (let offset = 0; offset < 200; offset += 1) {
        const port = startPort + offset;
        if (usedPorts.has(port)) continue;
        if (await isPortAvailable(host, port)) return port;
    }
    throw new Error('No local game server ports are available.');
}

function activeGameInstances(app, gameId) {
    const active = [];
    for (const [instanceId, instance] of app.locals.gameInstances.entries()) {
        const running = instance.manual || isChildRunning(instance.child);
        if (!running) {
            clearEmptyShutdown(instance);
            app.locals.gameInstances.delete(instanceId);
            continue;
        }
        if (instance.status === 'starting' && isChildRunning(instance.child)) {
            instance.status = 'running';
        }
        if (instance.gameId === gameId) active.push(instance);
    }
    return active;
}

function serializeGameInstance(instance) {
    return {
        id: instance.id,
        host: instance.host,
        port: instance.port,
        status: instance.status,
        processId: instance.processId,
        startedAt: instance.startedAt,
        playerCount: Number(instance.playerCount || 0),
        lastHeartbeatAt: instance.lastHeartbeatAt || null,
        emptySince: instance.emptySince || null,
        emptyShutdownAt: instance.emptyShutdownAt || null
    };
}

function reserveGameInstanceForLaunch(app, instance) {
    if (!instance || instance.status === 'manual' || Number(instance.playerCount || 0) > 0) return;
    instance.emptySince = new Date().toISOString();
    scheduleEmptyShutdown(app, instance);
}

function updateInstanceHeartbeat(app, instance, playerCount) {
    instance.playerCount = playerCount;
    instance.lastHeartbeatAt = new Date().toISOString();

    if (playerCount > 0) {
        if (!instance.manual) {
            instance.status = 'running';
        }
        instance.emptySince = null;
        clearEmptyShutdown(instance);
        return;
    }

    if (!instance.manual && instance.status === 'starting' && isChildRunning(instance.child)) {
        instance.status = 'running';
    }
    if (!instance.emptySince) {
        instance.emptySince = new Date().toISOString();
    }
    scheduleEmptyShutdown(app, instance);
}

async function ensureGameInstance(app, game, options = {}) {
    const config = app.locals.config;
    const existing = activeGameInstances(app, game.id);
    if (!options.forceNew && existing.length > 0) {
        return existing[0];
    }

    const usedPorts = new Set([...app.locals.gameInstances.values()].map((instance) => instance.port));
    const port = await findAvailablePort(config.gameServerHost || '127.0.0.1', config.gameServerBasePort || 7777, usedPorts);
    const instance = {
        id: crypto.randomUUID(),
        gameId: game.id,
        host: config.gameServerHost || '127.0.0.1',
        port,
        status: 'manual',
        manual: true,
        startedAt: new Date().toISOString(),
        playerCount: 0,
        lastHeartbeatAt: null,
        emptySince: null,
        emptyShutdownAt: null,
        shutdownTimer: null,
        managerToken: crypto.randomBytes(24).toString('hex'),
        processId: null,
        command: ''
    };

    if (config.serverExecutablePath && fs.existsSync(config.serverExecutablePath) && game.world_path) {
        instance.manual = false;
        const args = [
            '--port', String(port),
            '--world', game.world_path,
            '--web', config.publicBaseUrl || `http://localhost:${config.port || 3000}`,
            '--instance', instance.id,
            '--instance-token', instance.managerToken
        ];
        const child = spawn(config.serverExecutablePath, args, {
            cwd: config.repoRoot || path.resolve(__dirname, '..'),
            detached: true,
            stdio: 'ignore'
        });
        child.unref();
        instance.child = child;
        instance.processId = child.pid || null;
        instance.status = 'starting';
        instance.command = `"${config.serverExecutablePath}" ${args.map((arg) => `"${arg}"`).join(' ')}`;
        child.once('exit', () => {
            const current = app.locals.gameInstances.get(instance.id);
            if (current && current.child === child) {
                clearEmptyShutdown(current);
                app.locals.gameInstances.delete(instance.id);
            }
        });
        child.once('error', (error) => {
            instance.status = 'error';
            instance.lastError = error.message;
        });
    } else {
        instance.command = `Server.exe --port ${port} --world "${game.world_path}" --web "${config.publicBaseUrl || 'http://localhost:3000'}"`;
    }

    app.locals.gameInstances.set(instance.id, instance);
    return instance;
}

function buildClientLaunch(config, req, game, instance, token) {
    const webServerUrl = `${req.protocol}://${req.get('host')}`;
    const clientPath = config.clientExecutablePath || 'Client.exe';
    const args = [
        '--server', instance.host,
        '--port', String(instance.port),
        '--web', webServerUrl,
        '--token', token,
        '--game', String(game.id),
        '--connect'
    ];
    const params = new URLSearchParams({
        gameId: String(game.id),
        host: instance.host,
        port: String(instance.port),
        web: webServerUrl,
        token
    });

    return {
        host: instance.host,
        port: instance.port,
        webServerUrl,
        protocolUrl: `rblxclone://play?${params.toString()}`,
        command: `"${clientPath}" ${args.map((arg) => `"${arg}"`).join(' ')}`,
        args
    };
}

function launchClientIfAvailable(config, launch) {
    if (!config.clientExecutablePath || !fs.existsSync(config.clientExecutablePath)) {
        return { launched: false, reason: 'Client executable was not found.' };
    }

    const child = spawn(config.clientExecutablePath, launch.args, {
        cwd: config.repoRoot || path.resolve(__dirname, '..'),
        detached: true,
        stdio: 'ignore'
    });
    child.unref();
    return { launched: true, processId: child.pid || null };
}

function serializePublicUser(row, relationship = 'none', friendCount = 0) {
    return {
        id: row.id,
        username: row.username,
        createdAt: row.created_at,
        avatar: rowToAvatar(row.user_id ? row : null),
        stats: {
            playtimeSeconds: Number(row.playtime_seconds || 0),
            lastPlayedAt: row.last_played_at || null
        },
        friendCount,
        relationship
    };
}

async function getFriendCount(db, userId) {
    const row = await dbGet(db, `SELECT COUNT(*) AS count
        FROM friendships
        WHERE status = 'accepted'
          AND (requester_id = ? OR addressee_id = ?)`, [userId, userId]);
    return Number(row && row.count ? row.count : 0);
}

async function getFriendship(db, firstUserId, secondUserId) {
    return dbGet(db, `SELECT *
        FROM friendships
        WHERE (requester_id = ? AND addressee_id = ?)
           OR (requester_id = ? AND addressee_id = ?)`, [
        firstUserId,
        secondUserId,
        secondUserId,
        firstUserId
    ]);
}

function relationshipFor(viewerId, userId, friendship) {
    if (viewerId === userId) return 'self';
    if (!friendship) return 'none';
    if (friendship.status === 'accepted') return 'friends';
    return friendship.requester_id === viewerId ? 'outgoing' : 'incoming';
}

async function getPublicUser(db, userId, viewerId) {
    const row = await dbGet(db, `SELECT u.id, u.username, u.created_at,
            COALESCE(s.playtime_seconds, 0) AS playtime_seconds,
            s.last_played_at,
            a.user_id, a.face_id,
            a.head_color_r, a.head_color_g, a.head_color_b,
            a.torso_color_r, a.torso_color_g, a.torso_color_b,
            a.left_arm_color_r, a.left_arm_color_g, a.left_arm_color_b,
            a.right_arm_color_r, a.right_arm_color_g, a.right_arm_color_b,
            a.left_leg_color_r, a.left_leg_color_g, a.left_leg_color_b,
            a.right_leg_color_r, a.right_leg_color_g, a.right_leg_color_b
        FROM users u
        LEFT JOIN avatars a ON a.user_id = u.id
        LEFT JOIN user_stats s ON s.user_id = u.id
        WHERE u.id = ?`, [userId]);

    if (!row) return null;

    const [friendCount, friendship] = await Promise.all([
        getFriendCount(db, userId),
        viewerId ? getFriendship(db, viewerId, userId) : Promise.resolve(null)
    ]);
    return serializePublicUser(row, relationshipFor(viewerId, userId, friendship), friendCount);
}

async function getFriends(db, userId, viewerId, limit = 24) {
    const rows = await dbAll(db, `SELECT u.id, u.username, u.created_at,
            COALESCE(s.playtime_seconds, 0) AS playtime_seconds,
            s.last_played_at,
            a.user_id, a.face_id,
            a.head_color_r, a.head_color_g, a.head_color_b,
            a.torso_color_r, a.torso_color_g, a.torso_color_b,
            a.left_arm_color_r, a.left_arm_color_g, a.left_arm_color_b,
            a.right_arm_color_r, a.right_arm_color_g, a.right_arm_color_b,
            a.left_leg_color_r, a.left_leg_color_g, a.left_leg_color_b,
            a.right_leg_color_r, a.right_leg_color_g, a.right_leg_color_b
        FROM friendships f
        JOIN users u ON u.id = CASE WHEN f.requester_id = ? THEN f.addressee_id ELSE f.requester_id END
        LEFT JOIN avatars a ON a.user_id = u.id
        LEFT JOIN user_stats s ON s.user_id = u.id
        WHERE f.status = 'accepted'
          AND (f.requester_id = ? OR f.addressee_id = ?)
        ORDER BY lower(u.username)
        LIMIT ?`, [userId, userId, userId, limit]);

    return Promise.all(rows.map(async (row) => {
        const [friendCount, friendship] = await Promise.all([
            getFriendCount(db, row.id),
            viewerId ? getFriendship(db, viewerId, row.id) : Promise.resolve(null)
        ]);
        return serializePublicUser(row, relationshipFor(viewerId, row.id, friendship), friendCount);
    }));
}

async function getFriendRequests(db, userId, direction) {
    const column = direction === 'incoming' ? 'addressee_id' : 'requester_id';
    const otherColumn = direction === 'incoming' ? 'requester_id' : 'addressee_id';
    const rows = await dbAll(db, `SELECT u.id, u.username, u.created_at,
            COALESCE(s.playtime_seconds, 0) AS playtime_seconds,
            s.last_played_at,
            a.user_id, a.face_id,
            a.head_color_r, a.head_color_g, a.head_color_b,
            a.torso_color_r, a.torso_color_g, a.torso_color_b,
            a.left_arm_color_r, a.left_arm_color_g, a.left_arm_color_b,
            a.right_arm_color_r, a.right_arm_color_g, a.right_arm_color_b,
            a.left_leg_color_r, a.left_leg_color_g, a.left_leg_color_b,
            a.right_leg_color_r, a.right_leg_color_g, a.right_leg_color_b
        FROM friendships f
        JOIN users u ON u.id = f.${otherColumn}
        LEFT JOIN avatars a ON a.user_id = u.id
        LEFT JOIN user_stats s ON s.user_id = u.id
        WHERE f.status = 'pending'
          AND f.${column} = ?
        ORDER BY f.created_at DESC
        LIMIT 24`, [userId]);

    return Promise.all(rows.map(async (row) => {
        const friendCount = await getFriendCount(db, row.id);
        return serializePublicUser(row, direction, friendCount);
    }));
}

function extractBearerToken(req) {
    const authorization = req.get('authorization');
    if (!authorization) return '';

    const match = authorization.match(/^Bearer\s+(.+)$/i);
    return match ? match[1].trim() : '';
}

function generateToken(config, userId, username) {
    return jwt.sign({ userId, username }, config.jwtSecret, {
        subject: String(userId),
        expiresIn: config.jwtExpiresIn
    });
}

function createCorsOptions(config) {
    return {
        origin(origin, callback) {
            if (!origin) {
                callback(null, true);
                return;
            }

            if (config.corsOrigins.includes(origin)) {
                callback(null, true);
                return;
            }

            callback(new Error('Origin not allowed by CORS.'));
        }
    };
}

function requireJsonBody(req, res, next) {
    if (['POST', 'PUT', 'PATCH'].includes(req.method) && req.path.startsWith('/api/')) {
        if (!req.is('application/json')) {
            res.status(415).json({ error: 'Content-Type must be application/json.' });
            return;
        }
    }
    next();
}

function createApp(options = {}) {
    const config = options.config
        ? { ...loadConfig(options.env || process.env), ...options.config }
        : loadConfig(options.env || process.env);
    const db = options.db || openDatabase(config.databasePath);
    const app = express();
    const ready = initializeDatabase(db).then(() => seedStarterGame(db, config));

    app.locals.config = config;
    app.locals.db = db;
    app.locals.ready = ready;
    app.locals.gameInstances = new Map();

    app.use(helmet({
        contentSecurityPolicy: false
    }));
    app.use(cors(createCorsOptions(config)));
    app.use(requireJsonBody);
    app.use(express.json({ limit: config.jsonBodyLimit }));
    app.use(express.static(path.join(__dirname, 'public')));
    app.use('/vendor/three', express.static(path.dirname(require.resolve('three'))));

    const authLimiter = rateLimit({
        windowMs: config.authRateLimitWindowMs,
        limit: config.authRateLimitMax,
        standardHeaders: 'draft-7',
        legacyHeaders: false,
        message: { error: 'Too many authentication attempts. Try again later.' }
    });

    async function authenticate(req, res, next) {
        const token = extractBearerToken(req);
        if (!token) {
            res.status(401).json({ error: 'Authorization bearer token is required.' });
            return;
        }

        try {
            await ready;
            const decoded = jwt.verify(token, config.jwtSecret);
            const userId = Number(decoded.userId || decoded.sub);
            if (!Number.isInteger(userId) || userId <= 0) {
                res.status(401).json({ error: 'Invalid or expired token.' });
                return;
            }

            const user = await dbGet(db, 'SELECT id, username FROM users WHERE id = ?', [userId]);
            if (!user) {
                res.status(401).json({ error: 'Invalid or expired token.' });
                return;
            }

            req.user = { id: user.id, username: user.username };
            next();
        } catch (error) {
            res.status(401).json({ error: 'Invalid or expired token.' });
        }
    }

    async function optionalAuthenticate(req, res, next) {
        const token = extractBearerToken(req);
        if (!token) {
            next();
            return;
        }

        try {
            await ready;
            const decoded = jwt.verify(token, config.jwtSecret);
            const userId = Number(decoded.userId || decoded.sub);
            const user = Number.isInteger(userId)
                ? await dbGet(db, 'SELECT id, username FROM users WHERE id = ?', [userId])
                : null;
            if (user) {
                req.user = { id: user.id, username: user.username };
            }
        } catch (error) {
            // Public game pages should still load if a stale browser token exists.
        }
        next();
    }

    app.post('/api/signup', authLimiter, async (req, res, next) => {
        try {
            await ready;
            const { username, password } = req.body || {};
            const validationError = validateCredentials(username, password);
            if (validationError) {
                res.status(400).json({ error: validationError });
                return;
            }

            const hashedPassword = await bcrypt.hash(password, config.bcryptRounds);
            const result = await dbRun(db, 'INSERT INTO users (username, password_hash) VALUES (?, ?)', [
                username,
                hashedPassword
            ]);

            const token = generateToken(config, result.lastID, username);
            res.status(201).json({
                success: true,
                token,
                userId: result.lastID,
                username
            });
        } catch (error) {
            if (error && String(error.message).includes('UNIQUE')) {
                res.status(409).json({ error: 'Username already exists.' });
                return;
            }
            next(error);
        }
    });

    app.post('/api/login', authLimiter, async (req, res, next) => {
        try {
            await ready;
            const { username, password } = req.body || {};
            const validationError = validateLoginInput(username, password);
            if (validationError) {
                res.status(400).json({ error: validationError });
                return;
            }

            const user = await dbGet(db, 'SELECT id, username, password_hash FROM users WHERE username = ?', [username]);
            if (!user) {
                res.status(401).json({ error: 'Invalid username or password.' });
                return;
            }

            const validPassword = await bcrypt.compare(password, user.password_hash);
            if (!validPassword) {
                res.status(401).json({ error: 'Invalid username or password.' });
                return;
            }

            const token = generateToken(config, user.id, user.username);
            res.json({
                success: true,
                token,
                userId: user.id,
                username: user.username
            });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/verify', async (req, res, next) => {
        try {
            await ready;
            let token = extractBearerToken(req);
            if (!token && req.body && typeof req.body.token === 'string') {
                res.set('Deprecation', 'true');
                token = req.body.token;
            }

            if (!token) {
                res.status(401).json({ error: 'Authorization bearer token is required.' });
                return;
            }

            const decoded = jwt.verify(token, config.jwtSecret);
            const userId = Number(decoded.userId || decoded.sub);
            const user = Number.isInteger(userId)
                ? await dbGet(db, 'SELECT id, username FROM users WHERE id = ?', [userId])
                : null;

            if (!user) {
                res.status(401).json({ error: 'Invalid or expired token.' });
                return;
            }

            res.json({
                success: true,
                userId: user.id,
                username: user.username
            });
        } catch (error) {
            res.status(401).json({ error: 'Invalid or expired token.' });
        }
    });

    app.get('/api/me/social', authenticate, async (req, res, next) => {
        try {
            const [profile, friends, incomingRequests, outgoingRequests] = await Promise.all([
                getPublicUser(db, req.user.id, req.user.id),
                getFriends(db, req.user.id, req.user.id, 48),
                getFriendRequests(db, req.user.id, 'incoming'),
                getFriendRequests(db, req.user.id, 'outgoing')
            ]);

            res.json({
                success: true,
                profile,
                friends,
                incomingRequests,
                outgoingRequests
            });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/me/playtime', authenticate, async (req, res, next) => {
        try {
            const seconds = clampPlaytimeSeconds(req.body && req.body.seconds);
            if (!seconds) {
                res.status(400).json({ error: 'Playtime seconds must be a positive number.' });
                return;
            }

            await dbRun(db, `INSERT INTO user_stats (user_id, playtime_seconds, last_played_at, updated_at)
                    VALUES (?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)
                    ON CONFLICT(user_id) DO UPDATE SET
                        playtime_seconds = playtime_seconds + excluded.playtime_seconds,
                        last_played_at = CURRENT_TIMESTAMP,
                        updated_at = CURRENT_TIMESTAMP`, [req.user.id, seconds]);

            const profile = await getPublicUser(db, req.user.id, req.user.id);
            res.json({ success: true, profile });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/game-instances/:id/heartbeat', async (req, res, next) => {
        try {
            await ready;
            const instance = app.locals.gameInstances.get(String(req.params.id || ''));
            if (!instance) {
                res.status(404).json({ error: 'Game instance not found.' });
                return;
            }

            const managerToken = extractBearerToken(req);
            if (!managerToken || managerToken !== instance.managerToken) {
                res.status(403).json({ error: 'Game instance token is invalid.' });
                return;
            }

            const playerCount = parsePlayerCount(req.body && req.body.playerCount);
            if (playerCount < 0) {
                res.status(400).json({ error: 'Player count must be a number from 0 to 200.' });
                return;
            }

            updateInstanceHeartbeat(app, instance, playerCount);
            res.json({ success: true, server: serializeGameInstance(instance) });
        } catch (error) {
            next(error);
        }
    });

    app.get('/api/users/search', authenticate, async (req, res, next) => {
        try {
            const query = String(req.query.q || '').trim().slice(0, 30);
            const limit = 12;
            const rows = query
                ? await dbAll(db, `SELECT u.id, u.username, u.created_at,
                        COALESCE(s.playtime_seconds, 0) AS playtime_seconds,
                        s.last_played_at,
                        a.user_id, a.face_id,
                        a.head_color_r, a.head_color_g, a.head_color_b,
                        a.torso_color_r, a.torso_color_g, a.torso_color_b,
                        a.left_arm_color_r, a.left_arm_color_g, a.left_arm_color_b,
                        a.right_arm_color_r, a.right_arm_color_g, a.right_arm_color_b,
                        a.left_leg_color_r, a.left_leg_color_g, a.left_leg_color_b,
                        a.right_leg_color_r, a.right_leg_color_g, a.right_leg_color_b
                    FROM users u
                    LEFT JOIN avatars a ON a.user_id = u.id
                    LEFT JOIN user_stats s ON s.user_id = u.id
                    WHERE u.id = ?
                       OR lower(u.username) LIKE ? ESCAPE '\\'
                    ORDER BY CASE WHEN lower(u.username) = lower(?) THEN 0 ELSE 1 END,
                             lower(u.username)
                    LIMIT ?`, [
                    parseUserId(query),
                    `%${escapeLike(query.toLowerCase())}%`,
                    query,
                    limit
                ])
                : await dbAll(db, `SELECT u.id, u.username, u.created_at,
                        COALESCE(s.playtime_seconds, 0) AS playtime_seconds,
                        s.last_played_at,
                        a.user_id, a.face_id,
                        a.head_color_r, a.head_color_g, a.head_color_b,
                        a.torso_color_r, a.torso_color_g, a.torso_color_b,
                        a.left_arm_color_r, a.left_arm_color_g, a.left_arm_color_b,
                        a.right_arm_color_r, a.right_arm_color_g, a.right_arm_color_b,
                        a.left_leg_color_r, a.left_leg_color_g, a.left_leg_color_b,
                        a.right_leg_color_r, a.right_leg_color_g, a.right_leg_color_b
                    FROM users u
                    LEFT JOIN avatars a ON a.user_id = u.id
                    LEFT JOIN user_stats s ON s.user_id = u.id
                    ORDER BY u.id DESC
                    LIMIT ?`, [limit]);

            const users = await Promise.all(rows
                .filter((row) => row.id !== req.user.id)
                .map(async (row) => {
                    const [friendCount, friendship] = await Promise.all([
                        getFriendCount(db, row.id),
                        getFriendship(db, req.user.id, row.id)
                    ]);
                    return serializePublicUser(row, relationshipFor(req.user.id, row.id, friendship), friendCount);
                }));

            res.json({ success: true, query, users });
        } catch (error) {
            next(error);
        }
    });

    app.get('/api/users/:id', authenticate, async (req, res, next) => {
        try {
            const userId = parseUserId(req.params.id);
            if (!userId) {
                res.status(400).json({ error: 'User ID must be a positive number.' });
                return;
            }

            const profile = await getPublicUser(db, userId, req.user.id);
            if (!profile) {
                res.status(404).json({ error: 'User not found.' });
                return;
            }

            const friends = await getFriends(db, userId, req.user.id, 24);
            res.json({ success: true, profile, friends });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/friends/request', authenticate, async (req, res, next) => {
        try {
            const targetUserId = parseUserId(req.body && req.body.userId);
            if (!targetUserId || targetUserId === req.user.id) {
                res.status(400).json({ error: 'Choose another user to add as a friend.' });
                return;
            }

            const targetUser = await dbGet(db, 'SELECT id FROM users WHERE id = ?', [targetUserId]);
            if (!targetUser) {
                res.status(404).json({ error: 'User not found.' });
                return;
            }

            const existing = await getFriendship(db, req.user.id, targetUserId);
            if (existing && existing.status === 'accepted') {
                res.status(409).json({ error: 'You are already friends.' });
                return;
            }

            if (existing && existing.requester_id === req.user.id) {
                const profile = await getPublicUser(db, targetUserId, req.user.id);
                res.json({ success: true, relationship: 'outgoing', profile });
                return;
            }

            if (existing && existing.addressee_id === req.user.id) {
                await dbRun(db, `UPDATE friendships
                    SET status = 'accepted', updated_at = CURRENT_TIMESTAMP
                    WHERE id = ?`, [existing.id]);
                const profile = await getPublicUser(db, targetUserId, req.user.id);
                res.json({ success: true, relationship: 'friends', profile });
                return;
            }

            await dbRun(db, `INSERT INTO friendships (requester_id, addressee_id, status)
                VALUES (?, ?, 'pending')`, [req.user.id, targetUserId]);
            const profile = await getPublicUser(db, targetUserId, req.user.id);
            res.status(201).json({ success: true, relationship: 'outgoing', profile });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/friends/respond', authenticate, async (req, res, next) => {
        try {
            const requesterId = parseUserId(req.body && req.body.userId);
            const action = String(req.body && req.body.action || '').toLowerCase();
            if (!requesterId || !['accept', 'decline'].includes(action)) {
                res.status(400).json({ error: 'Friend response requires a requester and action.' });
                return;
            }

            const existing = await dbGet(db, `SELECT *
                FROM friendships
                WHERE requester_id = ?
                  AND addressee_id = ?
                  AND status = 'pending'`, [requesterId, req.user.id]);

            if (!existing) {
                res.status(404).json({ error: 'Friend request not found.' });
                return;
            }

            if (action === 'accept') {
                await dbRun(db, `UPDATE friendships
                    SET status = 'accepted', updated_at = CURRENT_TIMESTAMP
                    WHERE id = ?`, [existing.id]);
            } else {
                await dbRun(db, 'DELETE FROM friendships WHERE id = ?', [existing.id]);
            }

            const profile = await getPublicUser(db, requesterId, req.user.id);
            res.json({
                success: true,
                relationship: action === 'accept' ? 'friends' : 'none',
                profile
            });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/friends/remove', authenticate, async (req, res, next) => {
        try {
            const targetUserId = parseUserId(req.body && req.body.userId);
            if (!targetUserId || targetUserId === req.user.id) {
                res.status(400).json({ error: 'Choose another user.' });
                return;
            }

            const targetUser = await dbGet(db, 'SELECT id FROM users WHERE id = ?', [targetUserId]);
            if (!targetUser) {
                res.status(404).json({ error: 'User not found.' });
                return;
            }

            await dbRun(db, `DELETE FROM friendships
                WHERE (requester_id = ? AND addressee_id = ?)
                   OR (requester_id = ? AND addressee_id = ?)`, [
                req.user.id,
                targetUserId,
                targetUserId,
                req.user.id
            ]);

            const profile = await getPublicUser(db, targetUserId, req.user.id);
            res.json({ success: true, relationship: 'none', profile });
        } catch (error) {
            next(error);
        }
    });

    app.get('/api/games', async (req, res, next) => {
        try {
            await ready;
            const rows = await dbAll(db, `${gameQueryBase()}
                WHERE g.is_public = 1
                ORDER BY g.published_at DESC, g.id DESC
                LIMIT 60`);
            res.json({ success: true, games: rows.map((row) => serializeGame(row)) });
        } catch (error) {
            next(error);
        }
    });

    app.get('/api/games/mine', authenticate, async (req, res, next) => {
        try {
            const rows = await dbAll(db, `${gameQueryBase()}
                WHERE g.owner_id = ?
                ORDER BY g.updated_at DESC, g.id DESC`, [req.user.id]);
            res.json({ success: true, games: rows.map((row) => serializeGame(row, true)) });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/games', authenticate, async (req, res, next) => {
        try {
            const game = await createGameRecord(db, config, req.user, req.body || {});
            res.status(201).json({ success: true, game: serializeGame(game, true) });
        } catch (error) {
            if (error.statusCode) {
                res.status(error.statusCode).json({ error: error.message });
                return;
            }
            next(error);
        }
    });

    app.post('/api/games/publish', authenticate, async (req, res, next) => {
        try {
            const gameId = parseUserId(req.body && req.body.gameId);
            const game = gameId
                ? await updateGameRecord(db, config, gameId, req.user, req.body || {})
                : await createGameRecord(db, config, req.user, req.body || {});
            res.status(gameId ? 200 : 201).json({ success: true, game: serializeGame(game, true) });
        } catch (error) {
            if (error.statusCode) {
                res.status(error.statusCode).json({ error: error.message });
                return;
            }
            next(error);
        }
    });

    app.get('/api/games/:id', optionalAuthenticate, async (req, res, next) => {
        try {
            await ready;
            const gameId = parseUserId(req.params.id);
            if (!gameId) {
                res.status(400).json({ error: 'Game ID must be a positive number.' });
                return;
            }

            const row = await getGameRow(db, gameId);
            if (!row || (!row.is_public && (!req.user || req.user.id !== row.owner_id))) {
                res.status(404).json({ error: 'Game not found.' });
                return;
            }

            res.json({
                success: true,
                game: serializeGame(row, req.user && req.user.id === row.owner_id),
                servers: activeGameInstances(app, row.id).map(serializeGameInstance)
            });
        } catch (error) {
            next(error);
        }
    });

    app.put('/api/games/:id', authenticate, async (req, res, next) => {
        try {
            const gameId = parseUserId(req.params.id);
            if (!gameId) {
                res.status(400).json({ error: 'Game ID must be a positive number.' });
                return;
            }

            const game = await updateGameRecord(db, config, gameId, req.user, req.body || {});
            res.json({ success: true, game: serializeGame(game, true) });
        } catch (error) {
            if (error.statusCode) {
                res.status(error.statusCode).json({ error: error.message });
                return;
            }
            next(error);
        }
    });

    app.get('/api/games/:id/servers', optionalAuthenticate, async (req, res, next) => {
        try {
            const gameId = parseUserId(req.params.id);
            const row = gameId ? await getGameRow(db, gameId) : null;
            if (!row || (!row.is_public && (!req.user || req.user.id !== row.owner_id))) {
                res.status(404).json({ error: 'Game not found.' });
                return;
            }

            res.json({
                success: true,
                servers: activeGameInstances(app, row.id).map(serializeGameInstance)
            });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/games/:id/play', authenticate, async (req, res, next) => {
        try {
            const gameId = parseUserId(req.params.id);
            const row = gameId ? await getGameRow(db, gameId) : null;
            if (!row || (!row.is_public && row.owner_id !== req.user.id)) {
                res.status(404).json({ error: 'Game not found.' });
                return;
            }

            const instance = await ensureGameInstance(app, row, {
                forceNew: Boolean(req.body && req.body.newServer)
            });
            reserveGameInstanceForLaunch(app, instance);
            await dbRun(db, `UPDATE games
                SET launch_count = launch_count + 1, updated_at = CURRENT_TIMESTAMP
                WHERE id = ?`, [row.id]);

            const token = extractBearerToken(req);
            const launch = buildClientLaunch(config, req, row, instance, token);
            const player = req.body && req.body.launchClient === false
                ? { launched: false, reason: 'Client launch was skipped.' }
                : launchClientIfAvailable(config, launch);

            res.json({
                success: true,
                game: serializeGame({ ...row, launch_count: Number(row.launch_count || 0) + 1 }),
                server: serializeGameInstance(instance),
                launch,
                player
            });
        } catch (error) {
            next(error);
        }
    });

    app.get('/api/avatar', authenticate, async (req, res, next) => {
        try {
            const row = await dbGet(db, 'SELECT * FROM avatars WHERE user_id = ?', [req.user.id]);
            res.json({
                success: true,
                avatar: rowToAvatar(row)
            });
        } catch (error) {
            next(error);
        }
    });

    app.post('/api/avatar', authenticate, async (req, res, next) => {
        try {
            const avatar = sanitizeAvatar(req.body && req.body.avatar);
            if (!avatar) {
                res.status(400).json({ error: 'Avatar data must contain six RGB arrays with values from 0 to 1.' });
                return;
            }

            const values = [
                req.user.id,
                avatar.headColor[0], avatar.headColor[1], avatar.headColor[2],
                avatar.torsoColor[0], avatar.torsoColor[1], avatar.torsoColor[2],
                avatar.leftArmColor[0], avatar.leftArmColor[1], avatar.leftArmColor[2],
                avatar.rightArmColor[0], avatar.rightArmColor[1], avatar.rightArmColor[2],
                avatar.leftLegColor[0], avatar.leftLegColor[1], avatar.leftLegColor[2],
                avatar.rightLegColor[0], avatar.rightLegColor[1], avatar.rightLegColor[2],
                avatar.faceId
            ];

            await dbRun(db, `INSERT INTO avatars (
                    user_id,
                    head_color_r, head_color_g, head_color_b,
                    torso_color_r, torso_color_g, torso_color_b,
                    left_arm_color_r, left_arm_color_g, left_arm_color_b,
                    right_arm_color_r, right_arm_color_g, right_arm_color_b,
                    left_leg_color_r, left_leg_color_g, left_leg_color_b,
                    right_leg_color_r, right_leg_color_g, right_leg_color_b,
                    face_id
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(user_id) DO UPDATE SET
                    head_color_r = excluded.head_color_r,
                    head_color_g = excluded.head_color_g,
                    head_color_b = excluded.head_color_b,
                    torso_color_r = excluded.torso_color_r,
                    torso_color_g = excluded.torso_color_g,
                    torso_color_b = excluded.torso_color_b,
                    left_arm_color_r = excluded.left_arm_color_r,
                    left_arm_color_g = excluded.left_arm_color_g,
                    left_arm_color_b = excluded.left_arm_color_b,
                    right_arm_color_r = excluded.right_arm_color_r,
                    right_arm_color_g = excluded.right_arm_color_g,
                    right_arm_color_b = excluded.right_arm_color_b,
                    left_leg_color_r = excluded.left_leg_color_r,
                    left_leg_color_g = excluded.left_leg_color_g,
                    left_leg_color_b = excluded.left_leg_color_b,
                    right_leg_color_r = excluded.right_leg_color_r,
                    right_leg_color_g = excluded.right_leg_color_g,
                    right_leg_color_b = excluded.right_leg_color_b,
                    face_id = excluded.face_id`, values);

            res.json({ success: true });
        } catch (error) {
            next(error);
        }
    });

    app.get('/', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'index.html'));
    });

    app.get('/login', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'login.html'));
    });

    app.get('/signup', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'signup.html'));
    });

    app.get('/dashboard', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'dashboard.html'));
    });

    app.get('/avatar', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'avatar.html'));
    });

    app.get('/search', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'search.html'));
    });

    app.get('/games', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'games.html'));
    });

    app.get('/create', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'create.html'));
    });

    app.get('/games/:id', (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'game.html'));
    });

    app.get(['/profile', '/profile/:id'], (req, res) => {
        res.sendFile(path.join(__dirname, 'public', 'profile.html'));
    });

    app.use((err, req, res, next) => {
        if (err instanceof SyntaxError && 'body' in err) {
            res.status(400).json({ error: 'Malformed JSON body.' });
            return;
        }

        if (err && err.message === 'Origin not allowed by CORS.') {
            res.status(403).json({ error: 'Origin is not allowed.' });
            return;
        }

        if (config.nodeEnv !== 'test') {
            console.error('Unhandled server error:', err && err.message ? err.message : err);
        }
        res.status(500).json({ error: 'Server error.' });
    });

    return app;
}

async function startServer() {
    const app = createApp();
    await app.locals.ready;
    const { port } = app.locals.config;
    const server = app.listen(port, () => {
        console.log(`Website server running on http://localhost:${port}`);
    });
    return { app, server };
}

if (require.main === module) {
    startServer().catch((error) => {
        console.error(error.message);
        process.exit(1);
    });
}

module.exports = {
    DEFAULT_AVATAR,
    createApp,
    loadConfig,
    startServer,
    validateCredentials,
    sanitizeAvatar
};
