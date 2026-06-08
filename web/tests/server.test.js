const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createApp, loadConfig } = require('../server');

function createTestContext() {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'rblx-web-'));
    const databasePath = path.join(directory, 'users.sqlite');
    const app = createApp({
        config: {
            nodeEnv: 'test',
            isProduction: false,
            port: 0,
            jwtSecret: 'test-secret-with-enough-length-for-auth',
            jwtExpiresIn: '1h',
            databasePath,
            corsOrigins: ['http://localhost:3000'],
            bcryptRounds: 4,
            authRateLimitWindowMs: 60 * 1000,
            authRateLimitMax: 1000,
            jsonBodyLimit: '16kb'
        }
    });

    return { app, directory };
}

async function withServer(callback) {
    const { app, directory } = createTestContext();
    await app.locals.ready;
    const server = await new Promise((resolve) => {
        const listener = app.listen(0, () => resolve(listener));
    });
    const baseUrl = `http://127.0.0.1:${server.address().port}`;

    try {
        await callback({ app, baseUrl });
    } finally {
        await new Promise((resolve) => server.close(resolve));
        await new Promise((resolve, reject) => app.locals.db.close((err) => (err ? reject(err) : resolve())));
        fs.rmSync(directory, { recursive: true, force: true });
    }
}

async function request(baseUrl, method, pathName, body, token) {
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers.Authorization = `Bearer ${token}`;

    const response = await fetch(`${baseUrl}${pathName}`, {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body)
    });

    const json = await response.json();
    return { response, json };
}

test('signup validates username and password input', async () => {
    await withServer(async ({ baseUrl }) => {
        const { response, json } = await request(baseUrl, 'POST', '/api/signup', {
            username: '../bad',
            password: 'short'
        });

        assert.equal(response.status, 400);
        assert.match(json.error, /Username/);
    });
});

test('production config rejects missing or placeholder jwt secrets', () => {
    assert.throws(() => loadConfig({
        NODE_ENV: 'production',
        JWT_SECRET: 'replace-this-with-a-long-random-secret-before-production'
    }), /JWT_SECRET/);
});

test('registers, rejects duplicates, logs in, and verifies bearer tokens', async () => {
    await withServer(async ({ baseUrl }) => {
        const signup = await request(baseUrl, 'POST', '/api/signup', {
            username: 'Player_One',
            password: 'correct-horse'
        });

        assert.equal(signup.response.status, 201);
        assert.equal(signup.json.success, true);
        assert.equal(signup.json.username, 'Player_One');
        assert.equal(typeof signup.json.token, 'string');

        const duplicate = await request(baseUrl, 'POST', '/api/signup', {
            username: 'Player_One',
            password: 'correct-horse'
        });
        assert.equal(duplicate.response.status, 409);

        const badLogin = await request(baseUrl, 'POST', '/api/login', {
            username: 'Player_One',
            password: 'wrong-password'
        });
        assert.equal(badLogin.response.status, 401);

        const login = await request(baseUrl, 'POST', '/api/login', {
            username: 'Player_One',
            password: 'correct-horse'
        });
        assert.equal(login.response.status, 200);
        assert.equal(login.json.success, true);

        const verify = await request(baseUrl, 'POST', '/api/verify', {}, login.json.token);
        assert.equal(verify.response.status, 200);
        assert.deepEqual({
            success: verify.json.success,
            userId: verify.json.userId,
            username: verify.json.username
        }, {
            success: true,
            userId: signup.json.userId,
            username: 'Player_One'
        });
    });
});

test('avatar endpoints require auth, validate shape, and persist colors', async () => {
    await withServer(async ({ baseUrl }) => {
        const signup = await request(baseUrl, 'POST', '/api/signup', {
            username: 'AvatarUser',
            password: 'correct-horse'
        });
        const token = signup.json.token;

        const unauthorized = await request(baseUrl, 'GET', '/api/avatar');
        assert.equal(unauthorized.response.status, 401);

        const invalidAvatar = await request(baseUrl, 'POST', '/api/avatar', {
            avatar: {
                headColor: [2, 0, 0]
            }
        }, token);
        assert.equal(invalidAvatar.response.status, 400);

        const avatar = {
            headColor: [0.1, 0.2, 0.3],
            torsoColor: [0.4, 0.5, 0.6],
            leftArmColor: [0.7, 0.8, 0.9],
            rightArmColor: [0.2, 0.3, 0.4],
            leftLegColor: [0.5, 0.6, 0.7],
            rightLegColor: [0.8, 0.9, 1.0]
        };

        const save = await request(baseUrl, 'POST', '/api/avatar', { avatar }, token);
        assert.equal(save.response.status, 200);
        assert.equal(save.json.success, true);

        const load = await request(baseUrl, 'GET', '/api/avatar', undefined, token);
        assert.equal(load.response.status, 200);
        assert.deepEqual(load.json.avatar, avatar);
    });
});
