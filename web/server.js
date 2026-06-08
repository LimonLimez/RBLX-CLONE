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

    return sanitized;
}

function rowToAvatar(row) {
    if (!row) {
        return { ...DEFAULT_AVATAR };
    }

    return {
        headColor: [row.head_color_r, row.head_color_g, row.head_color_b],
        torsoColor: [row.torso_color_r, row.torso_color_g, row.torso_color_b],
        leftArmColor: [row.left_arm_color_r, row.left_arm_color_g, row.left_arm_color_b],
        rightArmColor: [row.right_arm_color_r, row.right_arm_color_g, row.right_arm_color_b],
        leftLegColor: [row.left_leg_color_r, row.left_leg_color_g, row.left_leg_color_b],
        rightLegColor: [row.right_leg_color_r, row.right_leg_color_g, row.right_leg_color_b]
    };
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
                avatar.rightLegColor[0], avatar.rightLegColor[1], avatar.rightLegColor[2]
            ];

            await dbRun(db, `INSERT INTO avatars (
                    user_id,
                    head_color_r, head_color_g, head_color_b,
                    torso_color_r, torso_color_g, torso_color_b,
                    left_arm_color_r, left_arm_color_g, left_arm_color_b,
                    right_arm_color_r, right_arm_color_g, right_arm_color_b,
                    left_leg_color_r, left_leg_color_g, left_leg_color_b,
                    right_leg_color_r, right_leg_color_g, right_leg_color_b
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
                    right_leg_color_b = excluded.right_leg_color_b`, values);

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
