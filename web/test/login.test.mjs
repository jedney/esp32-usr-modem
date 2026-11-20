import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {loginError, startLogin} from '../src/login.js';
import {loadApp, ok, status, AUTH} from './app-harness.mjs';

const page = await readFile(new URL('../src/login.html', import.meta.url), 'utf8');
const tag = id => page.match(new RegExp(`<input id="${id}"[^>]*>`))?.[0] ?? '';

test('the login page is a real form that password managers recognise', () => {
    assert.match(page, /<form [^>]*method="post"[^>]*action="\/api\/login"/);
    assert.match(tag('username'), /name="username"/);
    assert.match(tag('username'), /type="text"/);
    assert.match(tag('username'), /autocomplete="username"/);
    assert.match(tag('username'), /autocapitalize="none"/);
    assert.match(tag('username'), /spellcheck="false"/);
    assert.doesNotMatch(tag('username'), /value=/);  // empty, and editable
    assert.doesNotMatch(tag('username'), /readonly|disabled/);
    assert.match(tag('password'), /name="password"/);
    assert.match(tag('password'), /type="password"/);
    assert.match(tag('password'), /autocomplete="current-password"/);
    assert.doesNotMatch(page, /autocomplete="off"|onpaste/);
    assert.match(page, /<label for="username">/);
    assert.match(page, /<label for="password">/);
    assert.match(page, /<button class="button primary login-submit" type="submit">/);
    assert.match(page, /id="login-error" role="alert" hidden/);
    assert.match(page, /admin/);
    assert.match(page, /form-action 'self'/);
});

test('a failed login explains itself once, without the error staying in the address', () => {
    assert.equal(loginError('?error=invalid'), 'Invalid username or password.');
    assert.match(loginError('?error=wait'), /Wait a second/);
    assert.match(loginError('?error=busy'), /Too many browsers/);
    assert.match(loginError('?error=unavailable'), /could not record/);
    assert.equal(loginError(''), null);
    assert.equal(loginError('?error=toString'), null);
    assert.equal(loginError('?error=<b>x</b>'), null);
    const elements = new Map();
    const document = {getElementById: id => {
        if (!elements.has(id)) elements.set(id, {hidden: true, textContent: '', attributes: {}, focused: false,
            setAttribute(k, v) { this.attributes[k] = v; }, focus() { this.focused = true; }});
        return elements.get(id);
    }};
    const replaced = [];
    startLogin({document, location: {search: '?error=invalid', pathname: '/login'},
        history: {replaceState: (state, title, url) => replaced.push(url)}});
    assert.equal(document.getElementById('login-error').textContent, 'Invalid username or password.');
    assert.equal(document.getElementById('login-error').hidden, false);
    assert.equal(document.getElementById('password').attributes['aria-describedby'], 'login-error');
    assert.deepEqual(replaced, ['/login']);
    assert.equal(document.getElementById('username').focused, true);
    elements.clear();
    startLogin({document, location: {search: '', pathname: '/login'}, history: {replaceState: () => assert.fail()}});
    assert.equal(document.getElementById('login-error').hidden, true);
});

test('the terminal checks the login before connecting, and goes to the login page without one', async t => {
    const app = await loadApp(t, path => path === 'api/auth' ? status(401) : ok());
    assert.equal(app.fetches[0].path, 'api/auth');
    assert.equal(app.fetches[0].options.cache, 'no-store');
    assert.deepEqual(app.replaced, ['login']);
    assert.equal(app.sockets.length, 0);
    assert.ok(!app.fetches.some(f => f.path === 'api/session'));
});

test('with a login the terminal connects, then rechecks at the deadline and when the tab returns', async t => {
    let auth = ok(AUTH);
    const app = await loadApp(t, path => path === 'api/auth' ? auth : ok());
    assert.deepEqual(app.fetches.map(f => f.path), ['api/auth', 'api/session']);
    assert.equal(app.sockets.length, 1);
    assert.ok([...app.timers.values()].includes((AUTH.expires + 1) * 1000));
    app.listeners.get('visibilitychange')();
    await app.settle();
    assert.equal(app.fetches.at(-1).path, 'api/auth');
    assert.deepEqual(app.replaced, []);
    app.sockets[0].open();
    app.term.write('secret output');
    auth = status(401);
    app.runTimers((AUTH.expires + 1) * 1000);
    await app.settle();
    assert.deepEqual(app.replaced, ['login']);
    assert.deepEqual(app.term.writes, []);
    assert.equal(app.menuClears, 1);
    assert.equal(app.sockets[0].readyState, 3);
});

test('a login ended in another tab stops reconnecting and shows the login page', async t => {
    let session = ok();
    const app = await loadApp(t, path => path === 'api/auth' ? ok(AUTH) : path === 'api/session' ? session : ok());
    app.sockets[0].open();
    session = status(401);  // the other tab logged out: the modem closed this socket
    app.sockets[0].onclose();
    app.runTimers(1000);
    await app.settle();
    assert.deepEqual(app.replaced, ['login']);
    const sessions = app.fetches.filter(f => f.path === 'api/session').length;
    for (const ms of [1000, 2000, 5000, 10000, 30000]) app.runTimers(ms);
    await app.settle();
    assert.equal(app.fetches.filter(f => f.path === 'api/session').length, sessions);  // no retry loop
});

test('Help > Log out ends the session with its CSRF token and clears the page', async t => {
    const app = await loadApp(t, path => path === 'api/auth' ? ok(AUTH) : ok());
    const ws = app.sockets[0];
    ws.open();
    ws.message({t: 'access', edit: false, state: 'read-only'});  // works for observers too
    ws.message({t: 'call', state: 'connected', browser: true, host: 'Demo BBS'});  // and during calls
    app.term.write('ATZ');
    await app.element('logout').onclick();
    await app.settle();
    const logout = app.fetches.find(f => f.path === 'api/logout');
    assert.equal(logout.options.method, 'POST');
    assert.equal(logout.options.headers['X-CSRF-Token'], AUTH.csrf);
    assert.ok(!ws.sent.includes('{"t":"hangup"}'));  // no terminal request, no confirmation
    assert.deepEqual(app.replaced, ['login']);
    assert.deepEqual(app.term.writes, []);
    assert.equal(app.menuClears, 1);
    assert.equal(app.menuOptions.state().call.state, 'idle');
    assert.equal(app.menuOptions.state().csrf, null);
    assert.equal(ws.readyState, 3);
});

test('a logout the modem did not take keeps the page and says so', async t => {
    const app = await loadApp(t, path => path === 'api/auth' ? ok(AUTH)
        : path === 'api/logout' ? status(403) : ok());
    app.sockets[0].open();
    await app.element('logout').onclick();
    await app.settle();
    assert.deepEqual(app.replaced, []);
    assert.ok(app.element('toasts').children.some(el => el.textContent.includes('Could not log out')));
});

test('logging out during an upload keeps the page until the upload settles', async t => {
    const app = await loadApp(t, path => path === 'api/auth' ? ok(AUTH) : ok());
    app.sockets[0].open();
    app.element('signed-out-login').hidden = true;  // as index.html has it
    app.menuOptions.state().upload = true;  // an accepted upload is still sending
    await app.element('logout').onclick();
    await app.settle();
    assert.deepEqual(app.replaced, []);  // navigating away would abort the upload
    assert.ok(app.document.body.classes.has('logged-out'));
    assert.equal(app.element('signed-out').hidden, false);
    assert.equal(app.element('signed-out-status').textContent, 'Finishing update…');
    assert.equal(app.element('signed-out-login').hidden, true);  // nothing to submit yet
    // The upload settles (here: the flow finds the page busy and does nothing more).
    app.element('menu-reason').querySelector = () => ({textContent: 'Image rejected.'});
    app.element('image').files = [undefined];
    await app.element('image').onchange();
    await app.settle();
    assert.equal(app.element('signed-out-status').textContent, 'Image rejected.');
    assert.equal(app.element('signed-out-login').hidden, false);
    assert.deepEqual(app.replaced, []);
});
