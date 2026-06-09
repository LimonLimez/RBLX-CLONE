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

async function getJson(url, token) {
    const headers = {};
    if (token) headers.Authorization = `Bearer ${token}`;

    const response = await fetch(url, { headers });
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

async function requireSession() {
    const session = await verifySession();
    if (!session) {
        window.location.href = '/login';
        return null;
    }
    return session;
}

function colorToCss(color) {
    return `rgb(${color.map((value) => Math.round(value * 255)).join(' ')})`;
}

function formatPlaytime(seconds) {
    const total = Math.max(0, Number(seconds || 0));
    if (total < 60) return total > 0 ? `${total}s` : '0m';

    const minutes = Math.floor(total / 60);
    if (minutes < 60) return `${minutes}m`;

    const hours = Math.floor(minutes / 60);
    const remainingMinutes = minutes % 60;
    return remainingMinutes ? `${hours}h ${remainingMinutes}m` : `${hours}h`;
}

function formatJoined(value) {
    const date = value ? new Date(value) : null;
    if (!date || Number.isNaN(date.getTime())) return 'Joined recently';
    return `Joined ${date.toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: 'numeric' })}`;
}

function profileHref(user) {
    return `/profile/${user.id}`;
}

function createMiniAvatar(avatar) {
    const figure = document.createElement('div');
    figure.className = 'mini-avatar';
    figure.setAttribute('aria-hidden', 'true');

    const parts = [
        ['mini-head', avatar.headColor],
        ['mini-torso', avatar.torsoColor],
        ['mini-left-arm', avatar.leftArmColor],
        ['mini-right-arm', avatar.rightArmColor],
        ['mini-left-leg', avatar.leftLegColor],
        ['mini-right-leg', avatar.rightLegColor]
    ];

    for (const [className, color] of parts) {
        const part = document.createElement('span');
        part.className = className;
        part.style.background = colorToCss(color);
        figure.appendChild(part);
    }

    return figure;
}

function emptyMessage(message) {
    const paragraph = document.createElement('p');
    paragraph.className = 'empty-state';
    paragraph.textContent = message;
    return paragraph;
}

function relationshipCopy(relationship) {
    if (relationship === 'friends') return 'Friends';
    if (relationship === 'outgoing') return 'Request sent';
    if (relationship === 'incoming') return 'Request received';
    if (relationship === 'self') return 'Your profile';
    return 'Not friends';
}

function makeButton(label, className, onClick) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = className || 'button';
    button.textContent = label;
    button.addEventListener('click', (event) => {
        Promise.resolve(onClick(event)).catch((error) => {
            window.alert(error.message || 'That action could not be completed.');
        });
    });
    return button;
}

async function runFriendAction(buttons, action) {
    buttons.forEach((button) => {
        button.disabled = true;
        button.dataset.originalText = button.textContent;
        button.textContent = 'Working';
    });

    try {
        await action();
    } finally {
        buttons.forEach((button) => {
            button.disabled = false;
            button.textContent = button.dataset.originalText || button.textContent;
            delete button.dataset.originalText;
        });
    }
}

function createRelationshipActions(user, onChanged) {
    const token = getToken();
    const actions = document.createElement('div');
    actions.className = 'user-actions';

    if (user.relationship === 'self') {
        const link = document.createElement('a');
        link.className = 'button secondary small';
        link.href = '/avatar';
        link.textContent = 'Edit avatar';
        actions.appendChild(link);
        return actions;
    }

    if (user.relationship === 'none') {
        const add = makeButton('Add friend', 'button small', async () => {
            await runFriendAction([add], async () => {
                const { response, data } = await postJson('/api/friends/request', { userId: user.id }, token);
                if (!response.ok) throw new Error(data.error || 'Friend request failed.');
                onChanged?.(data.profile || user);
            });
        });
        actions.appendChild(add);
        return actions;
    }

    if (user.relationship === 'incoming') {
        const accept = makeButton('Accept', 'button small', async () => {
            await runFriendAction([accept, decline], async () => {
                const { response, data } = await postJson('/api/friends/respond', {
                    userId: user.id,
                    action: 'accept'
                }, token);
                if (!response.ok) throw new Error(data.error || 'Could not accept request.');
                onChanged?.(data.profile || user);
            });
        });
        const decline = makeButton('Decline', 'button secondary small', async () => {
            await runFriendAction([accept, decline], async () => {
                const { response, data } = await postJson('/api/friends/respond', {
                    userId: user.id,
                    action: 'decline'
                }, token);
                if (!response.ok) throw new Error(data.error || 'Could not decline request.');
                onChanged?.(data.profile || { ...user, relationship: 'none' });
            });
        });
        actions.append(accept, decline);
        return actions;
    }

    const removeLabel = user.relationship === 'friends' ? 'Unfriend' : 'Cancel request';
    const removeClass = user.relationship === 'friends' ? 'button danger small' : 'button secondary small';
    const remove = makeButton(removeLabel, removeClass, async () => {
        await runFriendAction([remove], async () => {
            const { response, data } = await postJson('/api/friends/remove', { userId: user.id }, token);
            if (!response.ok) throw new Error(data.error || 'Could not update friend.');
            onChanged?.(data.profile || user);
        });
    });
    actions.appendChild(remove);
    return actions;
}

function createUserCard(user, onChanged) {
    const card = document.createElement('article');
    card.className = 'user-card';

    const avatarLink = document.createElement('a');
    avatarLink.className = 'user-avatar-link';
    avatarLink.href = profileHref(user);
    avatarLink.setAttribute('aria-label', `${user.username} profile`);
    avatarLink.appendChild(createMiniAvatar(user.avatar || defaultAvatar));

    const content = document.createElement('div');
    content.className = 'user-card-content';

    const name = document.createElement('a');
    name.className = 'user-name';
    name.href = profileHref(user);
    name.textContent = user.username;

    const meta = document.createElement('p');
    meta.className = 'user-meta';
    meta.textContent = `ID ${user.id} · ${user.friendCount} friend${user.friendCount === 1 ? '' : 's'} · ${formatPlaytime(user.stats?.playtimeSeconds)} played`;

    const relation = document.createElement('p');
    relation.className = 'relationship-label';
    relation.textContent = relationshipCopy(user.relationship);

    content.append(name, meta, relation, createRelationshipActions(user, onChanged));
    card.append(avatarLink, content);
    return card;
}

function renderUserList(container, users, emptyText, onChanged) {
    if (!container) return;
    container.innerHTML = '';
    if (!users || users.length === 0) {
        container.appendChild(emptyMessage(emptyText));
        return;
    }

    for (const user of users) {
        container.appendChild(createUserCard(user, onChanged));
    }
}

function initLogoutButtons() {
    document.querySelectorAll('[data-logout]').forEach((button) => {
        button.addEventListener('click', () => {
            clearSession();
            window.location.href = '/login';
        });
    });
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

    const welcomeHeading = document.querySelector('[data-welcome-heading]');
    const profileSummary = document.querySelector('[data-profile-summary]');
    const friendsList = document.querySelector('[data-friends-list]');
    const incomingList = document.querySelector('[data-incoming-requests]');
    const outgoingList = document.querySelector('[data-outgoing-requests]');
    const status = document.querySelector('[data-dashboard-status]');

    verifySession()
        .then(async (session) => {
            if (!session) {
                window.location.href = '/login';
                return;
            }
            if (welcomeHeading) welcomeHeading.textContent = `Welcome, ${session.username}`;

            async function loadSocial() {
                setStatus(status, '', '');
                const { response, data } = await getJson('/api/me/social', getToken());
                if (!response.ok || !data.success) throw new Error(data.error || 'Could not load friends.');

                if (profileSummary) {
                    profileSummary.innerHTML = '';
                    const profile = document.createElement('div');
                    profile.className = 'profile-summary';
                    profile.appendChild(createMiniAvatar(data.profile.avatar || defaultAvatar));

                    const text = document.createElement('div');
                    const name = document.createElement('h2');
                    name.textContent = data.profile.username;
                    const meta = document.createElement('p');
                    meta.textContent = `${data.profile.friendCount} friend${data.profile.friendCount === 1 ? '' : 's'} · ${formatPlaytime(data.profile.stats.playtimeSeconds)} played`;
                    const link = document.createElement('a');
                    link.className = 'button small';
                    link.href = profileHref(data.profile);
                    link.textContent = 'View profile';
                    text.append(name, meta, link);
                    profile.appendChild(text);
                    profileSummary.appendChild(profile);
                }

                renderUserList(friendsList, data.friends, 'No friends yet. Search for people to add.', loadSocial);
                renderUserList(incomingList, data.incomingRequests, 'No friend requests right now.', loadSocial);
                renderUserList(outgoingList, data.outgoingRequests, 'No sent requests.', loadSocial);
            }

            await loadSocial();
        })
        .catch((error) => {
            setStatus(status, error.message || 'Could not load dashboard.', 'error');
        });
}

function initSearch() {
    const root = document.querySelector('[data-search-page]');
    if (!root) return;

    const form = document.querySelector('[data-search-form]');
    const input = document.querySelector('[data-search-input]');
    const results = document.querySelector('[data-search-results]');
    const status = document.querySelector('[data-search-status]');

    async function loadResults(query) {
        setStatus(status, '', '');
        results.innerHTML = '';
        results.appendChild(emptyMessage('Searching people'));

        const url = `/api/users/search?q=${encodeURIComponent(query || '')}`;
        const { response, data } = await getJson(url, getToken());
        if (!response.ok || !data.success) throw new Error(data.error || 'Search failed.');

        renderUserList(
            results,
            data.users,
            query ? `No people found for "${query}".` : 'No other people have signed up yet.',
            () => loadResults(input.value.trim())
        );
    }

    requireSession()
        .then((session) => {
            if (!session) return;
            const params = new URLSearchParams(window.location.search);
            const initialQuery = params.get('q') || '';
            input.value = initialQuery;

            form.addEventListener('submit', (event) => {
                event.preventDefault();
                const query = input.value.trim();
                const nextUrl = query ? `/search?q=${encodeURIComponent(query)}` : '/search';
                window.history.replaceState(null, '', nextUrl);
                loadResults(query).catch((error) => {
                    setStatus(status, error.message || 'Search failed.', 'error');
                });
            });

            return loadResults(initialQuery);
        })
        .catch((error) => {
            setStatus(status, error.message || 'Search failed.', 'error');
        });
}

function initProfilePage() {
    const root = document.querySelector('[data-profile-page]');
    if (!root) return;

    const title = document.querySelector('[data-profile-title]');
    const label = document.querySelector('[data-profile-label]');
    const stats = document.querySelector('[data-profile-stats]');
    const actions = document.querySelector('[data-profile-actions]');
    const avatar = document.querySelector('[data-profile-avatar]');
    const friends = document.querySelector('[data-profile-friends]');
    const status = document.querySelector('[data-profile-status]');
    let previewPromise = null;

    function selectedProfileId() {
        const match = window.location.pathname.match(/^\/profile\/(\d+)$/);
        return match ? match[1] : localStorage.getItem('userId');
    }

    async function renderProfile(profileData) {
        const { profile, friends: profileFriends } = profileData;
        document.title = `${profile.username} - RBLX Clone`;
        if (title) title.textContent = profile.username;
        if (label) label.textContent = profile.relationship === 'self' ? 'Your profile' : `User ID ${profile.id}`;

        if (stats) {
            stats.innerHTML = '';
            const rows = [
                ['Friends', String(profile.friendCount)],
                ['Playtime', formatPlaytime(profile.stats.playtimeSeconds)],
                ['Last played', profile.stats.lastPlayedAt ? new Date(profile.stats.lastPlayedAt).toLocaleString() : 'Not yet'],
                ['Joined', formatJoined(profile.createdAt).replace('Joined ', '')]
            ];

            for (const [name, value] of rows) {
                const row = document.createElement('div');
                row.className = 'stat-tile';
                const statName = document.createElement('span');
                statName.textContent = name;
                const statValue = document.createElement('strong');
                statValue.textContent = value;
                row.append(statName, statValue);
                stats.appendChild(row);
            }
        }

        if (actions) {
            actions.innerHTML = '';
            actions.appendChild(createRelationshipActions(profile, loadProfile));
        }

        if (avatar) {
            if (!previewPromise) {
                previewPromise = createAvatarPreview(avatar, () => {});
            }
            const preview = await previewPromise;
            preview.update(profile.avatar || defaultAvatar, '');
        }

        renderUserList(
            friends,
            profileFriends,
            profile.relationship === 'self' ? 'You have not added friends yet.' : `${profile.username} has no friends showing yet.`,
            loadProfile
        );
    }

    async function loadProfile() {
        setStatus(status, '', '');
        const profileId = selectedProfileId();
        if (!profileId) {
            window.location.href = '/dashboard';
            return;
        }

        const { response, data } = await getJson(`/api/users/${profileId}`, getToken());
        if (response.status === 404) {
            setStatus(status, 'User not found.', 'error');
            return;
        }
        if (!response.ok || !data.success) throw new Error(data.error || 'Profile could not load.');

        if (window.location.pathname === '/profile') {
            window.history.replaceState(null, '', profileHref(data.profile));
        }
        await renderProfile(data);
    }

    requireSession()
        .then((session) => {
            if (!session) return null;
            return loadProfile();
        })
        .catch((error) => {
            setStatus(status, error.message || 'Profile could not load.', 'error');
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

const avatarBlocks = [
    { id: 'head', label: 'Head', key: 'headColor', size: [1.2, 1.2, 1.0], position: [0, 4.8, 0] },
    { id: 'torso', label: 'Torso', key: 'torsoColor', size: [2.0, 2.4, 1.0], position: [0, 3.0, 0] },
    { id: 'leftArm', label: 'Left arm', key: 'leftArmColor', size: [0.85, 2.4, 1.0], position: [-1.425, 3.0, 0] },
    { id: 'rightArm', label: 'Right arm', key: 'rightArmColor', size: [0.85, 2.4, 1.0], position: [1.425, 3.0, 0] },
    { id: 'leftLeg', label: 'Left leg', key: 'leftLegColor', size: [1.0, 1.8, 1.0], position: [-0.5, 0.9, 0] },
    { id: 'rightLeg', label: 'Right leg', key: 'rightLegColor', size: [1.0, 1.8, 1.0], position: [0.5, 0.9, 0] }
];

function threeColor(color) {
    return color.map((value) => Math.round(value * 255) / 255);
}

function colorToHex(color) {
    return `#${color.map((value) => Math.round(value * 255).toString(16).padStart(2, '0')).join('')}`;
}

function hexToColor(hex) {
    const value = hex.replace('#', '');
    return [
        parseInt(value.slice(0, 2), 16) / 255,
        parseInt(value.slice(2, 4), 16) / 255,
        parseInt(value.slice(4, 6), 16) / 255
    ];
}

async function createAvatarPreview(container, onSelect) {
    const THREE = await import('/vendor/three/three.module.js');
    const scene = new THREE.Scene();
    const camera = new THREE.OrthographicCamera();
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, preserveDrawingBuffer: true });
    const root = new THREE.Group();
    const raycaster = new THREE.Raycaster();
    const pointer = new THREE.Vector2();
    const meshes = new Map();

    renderer.setClearColor(0xffffff, 1);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    renderer.shadowMap.enabled = false;
    renderer.domElement.className = 'avatar-canvas';
    renderer.domElement.setAttribute('aria-label', 'Drag to rotate avatar preview');
    renderer.domElement.setAttribute('role', 'img');

    scene.background = new THREE.Color(0xffffff);
    scene.add(root);
    root.rotation.set(-0.08, -0.35, 0);

    for (const block of avatarBlocks) {
        const geometry = new THREE.BoxGeometry(...block.size);
        const material = new THREE.MeshBasicMaterial({ color: 0xffffff });
        const mesh = new THREE.Mesh(geometry, material);
        const edge = new THREE.LineSegments(
            new THREE.EdgesGeometry(geometry),
            new THREE.LineBasicMaterial({ color: 0x050505 })
        );

        mesh.position.set(...block.position);
        mesh.userData.partId = block.id;
        mesh.userData.partKey = block.key;
        mesh.userData.edge = edge;
        mesh.add(edge);
        root.add(mesh);
        meshes.set(block.id, mesh);
    }

    container.replaceChildren(renderer.domElement);

    function updateCanvasProbe() {
        const gl = renderer.getContext();
        const width = renderer.domElement.width;
        const height = renderer.domElement.height;
        const readWidth = Math.min(140, width);
        const readHeight = Math.min(140, height);
        const x = Math.floor((width - readWidth) / 2);
        const y = Math.floor((height - readHeight) / 2);
        const pixels = new Uint8Array(readWidth * readHeight * 4);
        let nonWhite = 0;
        let colored = 0;
        let hash = 0;

        gl.readPixels(x, y, readWidth, readHeight, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        for (let index = 0; index < pixels.length; index += 4) {
            const red = pixels[index];
            const green = pixels[index + 1];
            const blue = pixels[index + 2];
            if (red < 245 || green < 245 || blue < 245) nonWhite += 1;
            if (Math.max(red, green, blue) - Math.min(red, green, blue) > 25) colored += 1;
            hash = (hash + ((index + 1) * (red + 3 * green + 7 * blue + 11 * pixels[index + 3]))) % 1000000007;
        }

        renderer.domElement.dataset.nonWhitePixels = String(nonWhite);
        renderer.domElement.dataset.coloredPixels = String(colored);
        renderer.domElement.dataset.renderHash = String(hash);
    }

    function render() {
        renderer.render(scene, camera);
        renderer.domElement.dataset.rotationX = root.rotation.x.toFixed(4);
        renderer.domElement.dataset.rotationY = root.rotation.y.toFixed(4);
        updateCanvasProbe();
    }

    function resize() {
        const width = Math.max(240, container.clientWidth);
        const height = Math.max(300, container.clientHeight);
        const aspect = width / height;
        const viewHeight = 6.4;

        camera.left = -viewHeight * aspect * 0.5;
        camera.right = viewHeight * aspect * 0.5;
        camera.top = viewHeight * 0.5;
        camera.bottom = -viewHeight * 0.5;
        camera.near = -20;
        camera.far = 20;
        camera.position.set(0, 2.65, 8);
        camera.lookAt(0, 2.65, 0);
        camera.updateProjectionMatrix();

        renderer.setSize(width, height, false);
        render();
    }

    function update(avatar, selected) {
        for (const block of avatarBlocks) {
            const mesh = meshes.get(block.id);
            mesh.material.color.setRGB(...threeColor(avatar[block.key]));
        }
        render();
    }

    const resizeObserver = new ResizeObserver(resize);
    resizeObserver.observe(container);
    resize();

    let dragging = false;
    let moved = false;
    let startX = 0;
    let startY = 0;
    let startRotationX = 0;
    let startRotationY = 0;
    let pointerDragActive = false;

    function setPointer(event) {
        const rect = renderer.domElement.getBoundingClientRect();
        pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
        pointer.y = -(((event.clientY - rect.top) / rect.height) * 2 - 1);
    }

    function hitTest(event) {
        setPointer(event);
        raycaster.setFromCamera(pointer, camera);
        const hits = raycaster.intersectObjects([...meshes.values()], false);
        return hits.length > 0 ? hits[0].object.userData.partId : '';
    }

    function beginDrag(clientX, clientY) {
        dragging = true;
        moved = false;
        startX = clientX;
        startY = clientY;
        startRotationX = root.rotation.x;
        startRotationY = root.rotation.y;
    }

    function moveDrag(clientX, clientY) {
        if (!dragging) return;

        const deltaX = clientX - startX;
        const deltaY = clientY - startY;
        moved = moved || Math.abs(deltaX) > 3 || Math.abs(deltaY) > 3;
        root.rotation.y = startRotationY + deltaX * 0.01;
        root.rotation.x = Math.max(-0.65, Math.min(0.65, startRotationX + deltaY * 0.008));
        render();
    }

    function endDrag(event) {
        dragging = false;
        if (moved) return;

        const partId = hitTest(event);
        if (partId) onSelect(partId);
    }

    renderer.domElement.addEventListener('pointerdown', (event) => {
        pointerDragActive = true;
        beginDrag(event.clientX, event.clientY);
        renderer.domElement.setPointerCapture(event.pointerId);
    });

    renderer.domElement.addEventListener('pointermove', (event) => {
        moveDrag(event.clientX, event.clientY);
    });

    renderer.domElement.addEventListener('pointerup', (event) => {
        if (renderer.domElement.hasPointerCapture(event.pointerId)) {
            renderer.domElement.releasePointerCapture(event.pointerId);
        }
        endDrag(event);
        pointerDragActive = false;
    });

    renderer.domElement.addEventListener('pointercancel', () => {
        dragging = false;
        pointerDragActive = false;
    });

    renderer.domElement.addEventListener('mousedown', (event) => {
        if (pointerDragActive) return;
        event.preventDefault();
        beginDrag(event.clientX, event.clientY);
    });

    window.addEventListener('mousemove', (event) => {
        if (pointerDragActive || !dragging) return;
        moveDrag(event.clientX, event.clientY);
    });

    window.addEventListener('mouseup', (event) => {
        if (pointerDragActive || !dragging) return;
        endDrag(event);
    });

    return { update };
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
    const colorPicker = document.querySelector('[data-color-picker]');
    const colorCode = document.querySelector('[data-color-code]');
    const status = document.querySelector('[data-status]');
    let preview = null;

    function selectedKey() {
        return avatarParts.find(([id]) => id === selected)[2];
    }

    function syncColorPicker() {
        const hex = colorToHex(avatar[selectedKey()]);
        colorPicker.value = hex;
        colorCode.value = hex.toUpperCase();
    }

    function syncPreview() {
        if (preview) preview.update(avatar, selected);
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
        drawTabs();
        syncColorPicker();
        syncPreview();
    }

    function updateSelectedColor() {
        avatar[selectedKey()] = hexToColor(colorPicker.value);
        syncColorPicker();
        syncPreview();
    }

    colorPicker.addEventListener('input', updateSelectedColor);

    createAvatarPreview(figure, selectPart)
        .then((createdPreview) => {
            preview = createdPreview;
            syncPreview();
        })
        .catch(() => {
            setStatus(status, '3D preview could not load.', 'error');
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
    initLogoutButtons();
    initLogin();
    initSignup();
    initDashboard();
    initSearch();
    initProfilePage();
    initAvatar();
});
