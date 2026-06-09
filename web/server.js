const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const bcrypt = require('bcrypt');
const jwt = require('jsonwebtoken');
const cors = require('cors');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const fs = require('fs');
const path = require('path');

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

    return {
        nodeEnv,
        isProduction,
        port: parseInteger(env.PORT, 3000),
        jwtSecret,
        jwtExpiresIn: env.JWT_EXPIRES_IN || '7d',
        databasePath: env.DATABASE_PATH || path.join(__dirname, 'users.db'),
        corsOrigins: splitCsv(env.CORS_ORIGIN).length > 0 ? splitCsv(env.CORS_ORIGIN) : defaultCorsOrigins,
        bcryptRounds: parseInteger(env.BCRYPT_ROUNDS, nodeEnv === 'test' ? 4 : 10),
        authRateLimitWindowMs: parseInteger(env.AUTH_RATE_LIMIT_WINDOW_MS, 15 * 60 * 1000),
        authRateLimitMax: parseInteger(env.AUTH_RATE_LIMIT_MAX, nodeEnv === 'test' ? 1000 : 20),
        jsonBodyLimit: env.JSON_BODY_LIMIT || '16kb'
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

function escapeLike(value) {
    return value.replace(/[\\%_]/g, (match) => `\\${match}`);
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
    const config = options.config || loadConfig(options.env || process.env);
    const db = options.db || openDatabase(config.databasePath);
    const app = express();
    const ready = initializeDatabase(db);

    app.locals.config = config;
    app.locals.db = db;
    app.locals.ready = ready;

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
