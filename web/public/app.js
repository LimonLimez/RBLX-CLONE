const tokenKey = 'authToken';

function getToken() {
    return localStorage.getItem(tokenKey);
}

function setStatus(element, message, type) {
    if (!element) return;
    element.textContent = message || '';
    element.className = `status ${type || ''}`.trim();
}

async function postJson(url, body, token) {
    const headers = { 'Content-Type': 'application/json' };
    if (token) headers.Authorization = `Bearer ${token}`;

    const response = await fetch(url, {
        method: 'POST',
        headers,
        body: JSON.stringify(body || {})
    });
    const data = await response.json();
    return { response, data };
}

function saveSession(data) {
    localStorage.setItem(tokenKey, data.token);
    localStorage.setItem('username', data.username);
    localStorage.setItem('userId', data.userId);
}

function clearSession() {
    localStorage.removeItem(tokenKey);
    localStorage.removeItem('username');
    localStorage.removeItem('userId');
}

async function verifySession() {
    const token = getToken();
    if (!token) return null;

    const { response, data } = await postJson('/api/verify', {}, token);
    if (!response.ok || !data.success) {
        clearSession();
        return null;
    }
    localStorage.setItem('username', data.username);
    localStorage.setItem('userId', data.userId);
    return data;
}

function initLogin() {
    const form = document.querySelector('[data-login-form]');
    if (!form) return;

    const status = document.querySelector('[data-status]');
    const submit = form.querySelector('button[type="submit"]');

    form.addEventListener('submit', async (event) => {
        event.preventDefault();
        setStatus(status, '', '');
        submit.disabled = true;
        submit.textContent = 'Signing in';

        try {
            const body = Object.fromEntries(new FormData(form).entries());
            const { response, data } = await postJson('/api/login', body);
            if (!response.ok) throw new Error(data.error || 'Login failed.');

            saveSession(data);
            setStatus(status, 'Signed in. Opening dashboard.', 'success');
            window.setTimeout(() => { window.location.href = '/dashboard'; }, 350);
        } catch (error) {
            setStatus(status, error.message || 'Network error.', 'error');
            submit.disabled = false;
            submit.textContent = 'Sign in';
        }
    });
}

function initSignup() {
    const form = document.querySelector('[data-signup-form]');
    if (!form) return;

    const status = document.querySelector('[data-status]');
    const submit = form.querySelector('button[type="submit"]');

    form.addEventListener('submit', async (event) => {
        event.preventDefault();
        setStatus(status, '', '');

        const body = Object.fromEntries(new FormData(form).entries());
        if (body.password !== body.confirmPassword) {
            setStatus(status, 'Passwords do not match.', 'error');
            return;
        }
        delete body.confirmPassword;

        submit.disabled = true;
        submit.textContent = 'Creating account';

        try {
            const { response, data } = await postJson('/api/signup', body);
            if (!response.ok) throw new Error(data.error || 'Signup failed.');

            saveSession(data);
            setStatus(status, 'Account created. Opening dashboard.', 'success');
            window.setTimeout(() => { window.location.href = '/dashboard'; }, 350);
        } catch (error) {
            setStatus(status, error.message || 'Network error.', 'error');
            submit.disabled = false;
            submit.textContent = 'Create account';
        }
    });
}

function initDashboard() {
    const dashboard = document.querySelector('[data-dashboard]');
    if (!dashboard) return;

    const username = document.querySelector('[data-username]');
    const userId = document.querySelector('[data-user-id]');
    const logout = document.querySelector('[data-logout]');

    logout?.addEventListener('click', () => {
        clearSession();
        window.location.href = '/login';
    });

    verifySession()
        .then((session) => {
            if (!session) {
                window.location.href = '/login';
                return;
            }
            username.textContent = session.username;
            userId.textContent = session.userId;
        })
        .catch(() => {
            clearSession();
            window.location.href = '/login';
        });
}

const avatarParts = [
    ['head', 'Head', 'headColor'],
    ['torso', 'Torso', 'torsoColor'],
    ['leftArm', 'Left arm', 'leftArmColor'],
    ['rightArm', 'Right arm', 'rightArmColor'],
    ['leftLeg', 'Left leg', 'leftLegColor'],
    ['rightLeg', 'Right leg', 'rightLegColor']
];

const defaultAvatar = {
    headColor: [0.8, 0.6, 0.4],
    torsoColor: [0.2, 0.4, 0.8],
    leftArmColor: [0.8, 0.6, 0.4],
    rightArmColor: [0.8, 0.6, 0.4],
    leftLegColor: [0.2, 0.6, 0.2],
    rightLegColor: [0.2, 0.6, 0.2]
};

const avatarLayout = {
    head: [38, 14, 24, 18],
    torso: [32, 32, 36, 30],
    leftArm: [20, 32, 12, 30],
    rightArm: [68, 32, 12, 30],
    leftLeg: [36, 62, 13, 28],
    rightLeg: [51, 62, 13, 28]
};

function rgb(color) {
    return `rgb(${color.map((value) => Math.round(value * 255)).join(', ')})`;
}

function initAvatar() {
    const root = document.querySelector('[data-avatar]');
    if (!root) return;

    const token = getToken();
    if (!token) {
        window.location.href = '/login';
        return;
    }

    let avatar = { ...defaultAvatar };
    let selected = 'head';
    const figure = document.querySelector('[data-avatar-figure]');
    const tabs = document.querySelector('[data-part-tabs]');
    const preview = document.querySelector('[data-color-preview]');
    const status = document.querySelector('[data-status]');
    const sliders = {
        red: document.querySelector('[data-slider="red"]'),
        green: document.querySelector('[data-slider="green"]'),
        blue: document.querySelector('[data-slider="blue"]')
    };

    function selectedKey() {
        return avatarParts.find(([id]) => id === selected)[2];
    }

    function syncSliders() {
        const color = avatar[selectedKey()];
        sliders.red.value = Math.round(color[0] * 255);
        sliders.green.value = Math.round(color[1] * 255);
        sliders.blue.value = Math.round(color[2] * 255);
        document.querySelector('[data-value="red"]').textContent = sliders.red.value;
        document.querySelector('[data-value="green"]').textContent = sliders.green.value;
        document.querySelector('[data-value="blue"]').textContent = sliders.blue.value;
        preview.style.background = rgb(color);
    }

    function drawAvatar() {
        figure.innerHTML = '';
        for (const [id, label, key] of avatarParts) {
            const [left, top, width, height] = avatarLayout[id];
            const part = document.createElement('button');
            part.type = 'button';
            part.className = `body-part${id === selected ? ' selected' : ''}`;
            part.style.left = `${left}%`;
            part.style.top = `${top}%`;
            part.style.width = `${width}%`;
            part.style.height = `${height}%`;
            part.style.background = rgb(avatar[key]);
            part.setAttribute('aria-label', label);
            part.addEventListener('click', () => selectPart(id));
            figure.appendChild(part);
        }
    }

    function drawTabs() {
        tabs.innerHTML = '';
        for (const [id, label] of avatarParts) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = `part-tab${id === selected ? ' active' : ''}`;
            button.textContent = label;
            button.addEventListener('click', () => selectPart(id));
            tabs.appendChild(button);
        }
    }

    function selectPart(id) {
        selected = id;
        drawAvatar();
        drawTabs();
        syncSliders();
    }

    function updateSelectedColor() {
        avatar[selectedKey()] = [
            Number(sliders.red.value) / 255,
            Number(sliders.green.value) / 255,
            Number(sliders.blue.value) / 255
        ];
        drawAvatar();
        syncSliders();
    }

    Object.values(sliders).forEach((slider) => {
        slider.addEventListener('input', updateSelectedColor);
    });

    document.querySelector('[data-save-avatar]').addEventListener('click', async () => {
        const button = document.querySelector('[data-save-avatar]');
        setStatus(status, '', '');
        button.disabled = true;
        button.textContent = 'Saving';

        try {
            const { response, data } = await postJson('/api/avatar', { avatar }, token);
            if (!response.ok || !data.success) throw new Error(data.error || 'Avatar save failed.');
            setStatus(status, 'Avatar saved.', 'success');
        } catch (error) {
            setStatus(status, error.message || 'Network error.', 'error');
        } finally {
            button.disabled = false;
            button.textContent = 'Save avatar';
        }
    });

    fetch('/api/avatar', { headers: { Authorization: `Bearer ${token}` } })
        .then((response) => response.json())
        .then((data) => {
            if (data.success && data.avatar) avatar = data.avatar;
            selectPart('head');
        })
        .catch(() => {
            selectPart('head');
            setStatus(status, 'Using local defaults until the server responds.', 'error');
        });
}

document.addEventListener('DOMContentLoaded', () => {
    initLogin();
    initSignup();
    initDashboard();
    initAvatar();
});
