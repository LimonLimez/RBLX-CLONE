const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function loadBrowserApp() {
    const appPath = path.join(__dirname, '..', 'public', 'app.js');
    const source = fs.readFileSync(appPath, 'utf8');
    const context = {
        URL,
        URLSearchParams,
        console,
        document: {
            addEventListener() {},
            querySelector() { return null; },
            querySelectorAll() { return []; }
        },
        localStorage: {
            getItem() { return null; },
            setItem() {},
            removeItem() {}
        },
        window: {
            location: { href: '' },
            setTimeout(callback) { callback(); }
        },
        fetch() {
            throw new Error('fetch should not run in browser utility tests');
        }
    };
    vm.createContext(context);
    vm.runInContext(source, context, { filename: appPath });
    return context;
}

test('player login redirects are limited to the local callback ports', () => {
    const { safePlayerRedirect } = loadBrowserApp();

    const accepted = safePlayerRedirect('http://127.0.0.1:39170/auth');
    assert.equal(accepted.toString(), 'http://127.0.0.1:39170/auth');

    const localhost = safePlayerRedirect('http://localhost:39189/auth?next=/dashboard');
    assert.equal(localhost.hostname, 'localhost');
    assert.equal(localhost.port, '39189');

    assert.equal(safePlayerRedirect('http://127.0.0.1:39169/auth'), null);
    assert.equal(safePlayerRedirect('http://127.0.0.1:39190/auth'), null);
    assert.equal(safePlayerRedirect('http://127.0.0.1:39170/not-auth'), null);
    assert.equal(safePlayerRedirect('https://127.0.0.1:39170/auth'), null);
    assert.equal(safePlayerRedirect('http://example.com:39170/auth'), null);
    assert.equal(safePlayerRedirect('/auth'), null);
});
