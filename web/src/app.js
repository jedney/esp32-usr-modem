// Browser modem terminal with automatic initial access, live sizing and a passive status bar.
//
// Talks to the modem over one WebSocket (see web/PROTOCOL.md):
//   binary frames = terminal bytes (UTF-8, ANSI) in both directions
//   text frames   = JSON status messages from the modem
//
// Demo mode (?demo, or opened from a file) simulates the modem so the page can be
// previewed without hardware.

import { Terminal } from './xterm.mjs';
import * as S from './status.js';
import {createUploadFlow, xhrSender} from './upload_flow.js';
import {createModemLink} from './link.js';
import {terminalGeometry, sizeMessage} from './geometry.js';
import {createIdleGuard} from './idle.js';
import {createToasts} from './toast.js';
import {createMenuBar} from './menu-view.js';
import {menuDisabledReason} from './menubar.js';
import {createCommandChannel} from './command-channel.js';

const COLS = 80;
const ROWS = 25;

// Classic VGA text-mode palette, as an ANSI BBS would have looked on a PC.
const VGA_THEME = {
    background: '#000000',
    foreground: '#aaaaaa',
    cursor: '#aaaaaa',
    cursorAccent: '#000000',
    selectionBackground: '#5555ff80',
    black: '#000000',
    red: '#aa0000',
    green: '#00aa00',
    yellow: '#aa5500',
    blue: '#0000aa',
    magenta: '#aa00aa',
    cyan: '#00aaaa',
    white: '#aaaaaa',
    brightBlack: '#555555',
    brightRed: '#ff5555',
    brightGreen: '#55ff55',
    brightYellow: '#ffff55',
    brightBlue: '#5555ff',
    brightMagenta: '#ff55ff',
    brightCyan: '#55ffff',
    brightWhite: '#ffffff',
};

const $ = (id) => document.getElementById(id);
const encoder = new TextEncoder();

const state = {
    call: { state: 'idle' },
    clock: { epoch: 0, tzo: 0, at: 0 },
    wifi: null,
    cell: null, // measured { w, h } per px of font size
    csrf: null, version: null, edit: false, upload: false, online: false, opening: false,
    reloadWhenOnline: false, // set after a successful update: reload once the modem is back
    hardware: null, busy: true,
    loggedOut: false, // the login ended: only an accepted upload may still be finishing
};
// The login session (GET api/auth): its CSRF token for logging out, and a timer at its deadline.
const login = {csrf: null, timer: null};
let menus = null;
let initialOpen = true, requestedOpen = false, pendingMenu = null;

function ensureTerminal(continuation = null) {
    if (state.upload) return;
    if (state.edit) { continuation?.(); term.focus(); return; }
    pendingMenu = continuation;
    if (state.opening) return;
    requestedOpen = true;
    if (state.online) requestTerminal();
}

function requestTerminal() {
    if (state.opening) return;
    requestedOpen = false;
    state.opening = true;
    updateControls();
    if (!openTerminal()) {
        state.opening = false;
        pendingMenu = null;
        updateControls();
        toasts.show('Could not open the terminal: the modem link is unavailable');
    }
}

function resumeMenu() {
    if (!state.edit || state.busy || !pendingMenu) return;
    const action = pendingMenu;
    pendingMenu = null;
    action();
}

const toasts = createToasts({container: $('toasts')});
const events = S.createStatusEvents(text => toasts.show(text));
const channel = createCommandChannel({
    send: bytes => send(bytes),
    changed: () => { state.busy = channel.busy(); updateControls(); resumeMenu(); },
    show: text => toasts.show(text),
});
const idle = createIdleGuard({
    prompt: seconds => {
        $('idle-guard').hidden = seconds === null;
        if (seconds !== null) $('idle-countdown').textContent = `Hanging up in ${seconds} seconds.`;
        else if (document.activeElement === $('here')) term.focus();
    },
    hangup: () => {
        events.away();
        if (!hangupCall()) {
            events.away(false);
            toasts.show('Could not hang up: the modem link is unavailable');
        } else {
            toasts.show('Session disconnected because you were away');
            closeTerminal();
        }
    },
});

function updateControls(showOperation = true) {
    $('session-disconnected').hidden = state.edit;
    $('session-disconnected').disabled = state.opening || state.upload;
    const calling = ['dialling','ringing','connected','ppp','slip'].includes(state.call.state);
    $('no-call').hidden = calling;
    $('hangup').hidden = !calling;
    if ($('upload')) $('upload').disabled = !!menuDisabledReason(state);
    $('hangup').disabled = !state.edit || !state.online || !state.call.browser ||
        !calling || state.upload;
    term.options.disableStdin = !state.edit || state.upload;
    if (showOperation && !state.upload) {
        const reason = state.opening ? 'Opening terminal…' : menuDisabledReason(state);
        const guidance = reason || 'Choose a menu item or type a command. AT*HELP for all commands.';
        $('menu-reason').title = guidance;
        $('menu-reason').querySelector('.guidance-long').textContent = guidance;
        $('menu-reason').querySelector('.guidance-short').textContent = !state.online ? 'Offline'
            : !state.edit ? 'Click terminal to reconnect' : reason ? 'Finish in terminal' : 'AT*HELP';
    }
    menus?.update();
    idle.connected(S.browserCallConnected(state.call, state.edit) && state.online);
}

// ---------------------------------------------------------------- status bar

function buildLeds() {
    const box = $('leds');
    for (const led of S.LEDS) {
        const el = document.createElement('span');
        el.className = 'led';
        el.id = `led-${led.id}`;
        el.title = led.title;
        el.innerHTML = `<i></i><b>${led.id}</b>`;
        box.appendChild(el);
    }
}

function showLeds(mask) {
    const on = S.decodeLeds(mask);
    for (const led of S.LEDS) {
        $(`led-${led.id}`).classList.toggle('on', on[led.id]);
    }
}

function modemNow() {
    const c = state.clock;
    return c.epoch > 0 ? c.epoch + (performance.now() - c.at) / 1000 : 0;
}

function showCall() {
    const d = S.describeCall(state.call);
    const el = $('call');
    el.className = `session-state ${d.cls}`;
    el.hidden = state.call.state === 'idle';
    $('call-label').textContent = state.call.state === 'idle' ? 'No call' : d.label;
    $('remote-label').textContent = S.describeRemote(state.call);
    $('remote').title = state.call.number ? `Remote host (dialled ${state.call.number})` : 'Remote host';
    $('remote').hidden = !state.call.host && !state.call.ip;
    showTimes();
    updateControls();
}

function showWifi() {
    const w = state.wifi;
    const el = $('wifi');
    if (!w || w.state !== 'up') {
        el.dataset.bars = '0';
        el.dataset.offline = 'true';
        el.title = w && w.state === 'connecting' ? 'Wi-Fi connecting' : 'Wi-Fi disconnected';
        el.setAttribute('aria-label', el.title);
        return;
    }
    el.dataset.bars = String(S.rssiBars(w.rssi));
    el.dataset.offline = 'false';
    el.title = `${w.ssid || 'Wi-Fi'} · ${w.rssi} dBm${w.ip ? ' · ' + w.ip : ''}`;
    el.setAttribute('aria-label', el.title);
}

function showTimes() {
    const now = modemNow();
    const clock = S.formatClock(now, state.clock.tzo);
    $('clock-label').textContent = now > 0 ? clock.split(' ').at(-1) : '—';
    $('clock-label').title = clock;
    const up = S.callUptime(state.call, now);
    $('uptime-label').textContent = up === null ? '--:--:--' : S.formatDuration(up);
    $('uptime-label').hidden = up === null;
}

function setLink(status) {
    events.link(status);
    state.online = status === 'online' || status === 'demo';
    document.body.classList.toggle('online', status === 'online');
    document.body.classList.toggle('offline', status === 'offline');
    $('link-label').textContent =
        { online: 'Modem online', offline: 'Modem offline', connecting: 'Connecting', demo: 'Demo modem' }[status];
    $('link').classList.toggle('offline', !state.online);
    if (status === 'demo') {
        document.body.classList.add('online');
    }
    updateControls();
}

function handleMessage(msg) {
    if (!msg) {
        return;
    }
    switch (msg.t) {
        case 'hello':
            state.csrf = msg.csrf;
            state.version = msg.ver;
            requestAnimationFrame(fitTerminal);
            break;
        case 'access':
            if (!S.terminalAccess(msg) && state.opening) pendingMenu = null;
            events.access(msg, state.opening);
            state.edit = S.terminalAccess(msg);
            state.opening = false;
            channel.reset();
            updateControls();
            if (state.edit) {
                reportedSize = null;
                requestAnimationFrame(fitTerminal);
                term.focus();
            } else clearTimeout(sizeTimer);
            if (initialOpen || requestedOpen) {
                initialOpen = false;
                requestedOpen = false;
                if (!state.edit) requestTerminal();
            }
            break;
        case 'hardware':
            state.hardware = msg;
            $('line-controls').hidden = !msg.active || msg.mode !== 'lines';
            $('stop-test').hidden = !msg.active;
            updateControls();
            break;
        case 'ready':
            channel.ready(msg);
            break;
        case 'led':
            showLeds(msg.m);
            break;
        case 'clock':
            state.clock = { epoch: msg.epoch, tzo: msg.tzo || 0, at: performance.now() };
            showTimes();
            break;
        case 'call':
            if (typeof msg.model === 'string' && msg.model && $('model-name').textContent !== msg.model) {
                $('model-name').textContent = msg.model;
                document.title = `Web Terminal - ${msg.model}`;
                requestAnimationFrame(fitTerminal);
            }
            events.call(msg);
            state.call = msg;
            showCall();
            idle.connected(S.browserCallConnected(msg, state.edit) && state.online);
            break;
        case 'wifi':
            state.wifi = msg;
            showWifi();
            break;
        default:
            break; // ignore unknown messages, so newer firmware doesn't break older pages
    }
}

// ---------------------------------------------------------------- terminal

const term = new Terminal({
    cols: COLS,
    rows: ROWS,
    fontFamily: "'IBM Plex Mono', monospace",
    fontSize: 20,
    lineHeight: 1.0,
    theme: VGA_THEME,
    cursorBlink: true,
    scrollback: 5000,
    disableStdin: true,
    drawBoldTextInBrightColors: true,
    allowTransparency: false,
});

let sizeTimer = null;
let reportedSize = null;
// Adapt cell geometry at a readable base size, then fit the font to that grid.
function fitTerminal() {
    const screen = $('screen');
    const style = getComputedStyle(screen);
    const width = screen.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
    const height = screen.clientHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom);
    if (!state.cell) {
        const el = term.element.querySelector('.xterm-screen');
        const size = term.options.fontSize;
        state.cell = { w: el.offsetWidth / term.cols / size, h: el.offsetHeight / term.rows / size };
    }
    const geometry = terminalGeometry(width, height, state.cell.w * 16, state.cell.h * 16);
    if (term.cols !== geometry.cols || term.rows !== geometry.rows) term.resize(geometry.cols, geometry.rows);
    const size = S.fitFontSize(width, height, term.cols, term.rows, state.cell.w, state.cell.h);
    if (size !== term.options.fontSize) {
        term.options.fontSize = size;
    }
    const message = sizeMessage(term.cols, term.rows);
    $('terminal-geometry').textContent = `UTF-8 · ${term.cols} × ${term.rows}`;
    if (state.edit && message !== reportedSize) {
        clearTimeout(sizeTimer);
        sizeTimer = setTimeout(() => {
            if (send(message)) reportedSize = message;
        }, 100);
    }
}

// ---------------------------------------------------------------- transport

let send = () => {};
let stopLink = () => {};
let reconnectModem = () => {};
let openTerminal = () => false;
let closeTerminal = () => {};
let hangupCall = () => false;
let pauseInput = () => {};

function connectModem() {
    const link = createModemLink({
        session: async () => {
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(), 5000);
            try {
                const reply = await fetch('api/session', {cache: 'no-store', signal: controller.signal});
                if (!reply.ok) {
                    // 503 while an update is written: other tabs wait longer before asking again.
                    // 401: this browser's login ended (logout in another tab, or expiry).
                    const error = new Error('Login unavailable');
                    error.busy = reply.status === 503;
                    error.auth = reply.status === 401;
                    throw error;
                }
                return (await reply.json()).token;
            } finally { clearTimeout(timer); }
        },
        socket: token => {
            const ws = new WebSocket(`ws://${location.host}/ws?token=${encodeURIComponent(token)}`);
            ws.binaryType = 'arraybuffer';
            return ws;
        },
        receive: data => {
            if (typeof data === 'string') handleMessage(S.parseMessage(data));
            else {
                const bytes = new Uint8Array(data);
                events.output(bytes);
                channel.output(bytes);
                term.write(bytes);
            }
        },
        status: status => {
            setLink(status);
            if (status !== 'online') return;
            if (state.reloadWhenOnline) location.reload(); // new firmware may bring new web files
            term.focus();
        },
        disconnect: () => {
            events.disconnected();
            state.csrf = null;
            state.edit = false;
            state.opening = false;
            state.hardware = null;
            pendingMenu = null;
            state.call = {state:'idle'};
            channel.reset();
            reportedSize = null;
            clearTimeout(sizeTimer);
            $('upload').disabled = true;
            $('line-controls').hidden = true;
            $('stop-test').hidden = true;
            showCall();
        },
        authLost: () => authLost(),
    });
    send = bytes => !state.upload && link.send(bytes);
    stopLink = () => link.stop();
    reconnectModem = () => link.reconnect();
    openTerminal = () => link.openTerminal();
    closeTerminal = () => link.closeTerminal();
    hangupCall = () => {
        channel.reset();
        return link.hangup();
    };
    pauseInput = busy => link.pause(busy);
    setInterval(() => link.poll(state.upload), 5000);
    link.start();
}

async function startDemo() {
    initialOpen = false;
    setLink('demo');
    const { startDemoModem } = await import('./demo.js');
    const modem = startDemoModem({
        message: handleMessage,
        write: data => {
            events.output(typeof data === 'string' ? encoder.encode(data) : data);
            channel.output(typeof data === 'string' ? encoder.encode(data) : data);
            term.write(data);
        },
    });
    send = bytes => {
        if (!state.edit || state.upload) return false;
        modem.receive(bytes);
        return true;
    };
    openTerminal = () => { modem.receive('{"t":"open"}'); return true; };
    closeTerminal = () => modem.close();
    hangupCall = () => { channel.reset(); modem.receive('{"t":"hangup"}'); return true; };
    requestTerminal();
    term.focus();
}

// ---------------------------------------------------------------- login

const demoMode = () => new URLSearchParams(location.search).has('demo') || location.protocol === 'file:' ||
    !!window.USR_MODEM_DEMO;

// 'ok', 'lost' (HTTP 401: the login ended) or 'unknown' (the modem did not answer).
async function checkAuth() {
    try {
        const reply = await fetch('api/auth', {cache: 'no-store', signal: AbortSignal.timeout(5000)});
        if (reply.status === 401) return 'lost';
        if (!reply.ok) return 'unknown';
        const auth = await reply.json();
        login.csrf = auth.csrf;
        clearTimeout(login.timer);
        // Check again just after the deadline; the modem ends the session itself.
        login.timer = setTimeout(recheckAuth, (auth.expires + 1) * 1000);
        return 'ok';
    } catch {
        return 'unknown';
    }
}
async function recheckAuth() {
    if (state.loggedOut || demoMode()) return;
    if (await checkAuth() === 'lost') authLost();
}

// The login ended here or in another tab. Forget everything this page showed, then show the
// login page. An accepted upload keeps running: the page stays and reports its result.
function authLost() {
    if (state.loggedOut) return;
    state.loggedOut = true;
    clearTimeout(login.timer);
    login.csrf = null;
    stopLink();
    idle.connected(false);
    menus?.clear();
    toasts.clear();
    term.clear();
    term.reset();
    Object.assign(state, {csrf: null, call: {state: 'idle'}, wifi: null, clock: {epoch: 0, tzo: 0, at: 0},
        hardware: null, edit: false, online: false, opening: false});
    pendingMenu = null;
    channel.reset();
    $('model-name').textContent = 'USR Modem';
    document.title = 'Web Terminal';
    if (!state.upload) {
        location.replace('login');
        return;
    }
    document.body.classList.add('logged-out');
    $('signed-out').hidden = false;
    $('signed-out-status').textContent = 'Finishing update…';
}

// Shows how an upload ended after logging out, then offers the login page. After an
// installed image the modem restarts: wait until it serves the login page again.
async function uploadFinishedSignedOut(result, text) {
    $('signed-out-status').textContent = result === 'installed' ? 'Update installed. The modem is restarting…' : text;
    if (result === 'installed') {
        await new Promise(resolve => setTimeout(resolve, 5000));
        for (;;) {
            try {
                if ((await fetch('login', {cache: 'no-store', signal: AbortSignal.timeout(3000)})).ok) break;
            } catch {
                // still restarting
            }
            await new Promise(resolve => setTimeout(resolve, 3000));
        }
        $('signed-out-status').textContent = 'Update installed. The modem has restarted.';
    }
    $('signed-out-login').hidden = false;
}

async function logout() {
    if (demoMode()) { toasts.show('Demo: there is no login to end.'); return; }
    if (state.loggedOut) return;
    try {
        if (!login.csrf && await checkAuth() === 'lost') { authLost(); return; }
        const reply = await fetch('api/logout', {method: 'POST', cache: 'no-store',
            headers: {'X-CSRF-Token': login.csrf ?? ''}, signal: AbortSignal.timeout(5000)});
        if (!reply.ok) throw new Error('Logout refused');
    } catch {
        // The login may still be valid: stay, or the login page would send the browser back here.
        toasts.show('Could not log out. Check the modem link and try again.');
        return;
    }
    authLost();
}

// ---------------------------------------------------------------- start

async function main() {
    menus = createMenuBar({state: () => state, run: (line, done) => channel.request(line, done),
        focus: () => term.focus(), notify: text => toasts.show(text), ensureTerminal});
    $('screen').onclick = () => { ensureTerminal(); };
    $('session-disconnected').onclick = event => { event.stopPropagation(); ensureTerminal(); };
    $('here').onclick = () => { idle.activity(); term.focus(); };
    $('hangup').onclick = () => { if (!hangupCall()) toasts.show('Could not hang up. Check the modem link.'); };
    $('dcd').onclick = () => channel.input(encoder.encode('d'));
    $('ri').onclick = () => channel.input(encoder.encode('r'));
    $('cts').onclick = () => channel.input(encoder.encode('c'));
    $('stop-test').onclick = () => { channel.input(new Uint8Array([0x18])); term.focus(); };
    $('upload').onclick = () => $('image').click();
    $('logout').onclick = () => logout();
    $('recent-console-history').onclick = async () => {
        try {
            const demo = demoMode();
            const text = demo ? 'DEMO: synthetic recent console history. No device services connected.\r\n'
                : await (async () => {
                    const reply = await fetch('api/boot', {cache:'no-store', signal:AbortSignal.timeout(5000)});
                    if (!reply.ok) throw new Error('History unavailable');
                    return reply.text();
                })();
            if (state.edit && state.online && !state.upload) term.write(text);
            term.focus();
        } catch { toasts.show('Could not read recent console history. Check the modem link and try again.'); }
    };
    const uploadFlow = createUploadFlow({
        state,
        show: text => {
            if (state.loggedOut) $('signed-out-status').textContent = text;
            $('menu-reason').title = text;
            $('menu-reason').querySelector('.guidance-long').textContent = text;
            $('menu-reason').querySelector('.guidance-short').textContent = 'Image upload';
        },
        setBusy: busy => {
            pauseInput(busy);
            updateControls(false);
            if (!busy && state.edit) requestAnimationFrame(fitTerminal);
        },
        confirm: text => window.confirm(text),
        read: async file => new Uint8Array(await file.arrayBuffer()),
        // Check the login is still valid before streaming a large body.
        login: async () => {
            const result = await checkAuth();
            if (result === 'lost') setTimeout(authLost);  // after the flow has finished
            return result === 'ok';
        },
        send: xhrSender(),
        installed: () => {
            if (state.loggedOut) return;
            toasts.show('Update uploaded — the modem is restarting');
            state.reloadWhenOnline = true;
            reconnectModem();
        },
    });
    $('image').onchange = async () => {
        try {
            const result = await uploadFlow($('image').files[0]);
            const text = $('menu-reason').querySelector('.guidance-long').textContent;
            if (state.loggedOut) uploadFinishedSignedOut(result, text);
            else if (['rejected', 'lost', 'failed'].includes(result)) toasts.show(text);
        } finally { $('image').value = ''; }
    };
    buildLeds();
    showLeds(0);
    try {
        await document.fonts.load("20px 'IBM Plex Mono'");
    } catch {
        // fall back to the browser's monospace font
    }
    term.open($('term'));
    requestAnimationFrame(fitTerminal);
    window.addEventListener('resize', fitTerminal);
    const input = bytes => {
        idle.activity();
        if (!channel.input(bytes)) toasts.show(state.edit
            ? 'Input could not be sent. Wait for the modem before pasting again.'
            : 'Click the terminal to reconnect before typing');
    };
    // xterm maps Enter to CR, Backspace to DEL (Ctrl-H sends BS), and arrows /
    // function keys to ANSI sequences. Preserve those bytes, including paste.
    term.onData(data => input(encoder.encode(data)));
    term.onBinary(data => input(Uint8Array.from(data, c => c.charCodeAt(0) & 0xff)));
    for (const event of ['keydown', 'pointerdown', 'pointermove', 'click', 'wheel']) {
        document.addEventListener(event, () => idle.activity(), {passive: true});
    }
    document.addEventListener('visibilitychange', () => {
        idle.poll();
        if (!document.hidden) recheckAuth();
    });
    setInterval(() => idle.poll(), 1000);
    setInterval(showTimes, 1000);
    showCall();
    showWifi();

    if (demoMode()) {
        startDemo();
    } else if (await checkAuth() === 'lost') {
        location.replace('login');
    } else {
        connectModem();
    }
}

main();
