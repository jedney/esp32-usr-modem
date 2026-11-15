// Browser modem terminal with explicit ownership, live sizing and a passive status bar.
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
};

const toasts = createToasts({container: $('toasts')});
const events = S.createStatusEvents(text => toasts.show(text));
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
        }
    },
});

function updateControls(showOperation = true) {
    $('open-terminal').disabled = !state.online || state.edit || state.opening || state.upload;
    $('close-terminal').disabled = !state.edit || state.upload;
    $('upload').disabled = !state.edit || state.upload;
    term.options.disableStdin = !state.edit || state.upload;
    if (showOperation && !state.upload) {
        $('operation').textContent = state.edit ? 'Terminal open — type AT*HELP for commands'
            : state.opening ? 'Opening terminal…' : 'Read-only live status — open the terminal to type';
    }
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
    el.className = `item ${d.cls}`;
    $('call-label').textContent = d.label;
    $('remote-label').textContent = S.describeRemote(state.call);
    $('remote').title = state.call.number ? `Remote host (dialled ${state.call.number})` : 'Remote host';
    showTimes();
}

function showWifi() {
    const w = state.wifi;
    const el = $('wifi');
    if (!w || w.state !== 'up') {
        el.dataset.bars = '0';
        $('wifi-label').textContent = w && w.state === 'connecting' ? 'Wi-Fi connecting' : 'No Wi-Fi';
        return;
    }
    el.dataset.bars = String(S.rssiBars(w.rssi));
    $('wifi-label').textContent = `${w.ssid || 'Wi-Fi'} ${w.rssi} dBm`;
    el.title = `Modem Wi-Fi: ${w.ssid || ''}, ${w.ip || ''}`;
}

function showTimes() {
    const now = modemNow();
    $('clock-label').textContent = S.formatClock(now, state.clock.tzo);
    const up = S.callUptime(state.call, now);
    $('uptime-label').textContent = up === null ? '--:--:--' : S.formatDuration(up);
}

function setLink(status) {
    events.link(status);
    state.online = status === 'online' || status === 'demo';
    document.body.classList.toggle('online', status === 'online');
    document.body.classList.toggle('offline', status === 'offline');
    $('link-label').textContent =
        { online: 'Online', offline: 'Offline', connecting: 'Connecting', demo: 'Demo' }[status];
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
            document.title = `${msg.name || 'USR Modem'} v${msg.ver || '?'}`;
            requestAnimationFrame(fitTerminal);
            break;
        case 'access':
            events.access(msg, state.opening);
            state.edit = S.terminalAccess(msg);
            state.opening = false;
            updateControls();
            if (state.edit) {
                reportedSize = null;
                requestAnimationFrame(fitTerminal);
                term.focus();
            } else clearTimeout(sizeTimer);
            break;
        case 'hardware':
            $('line-controls').hidden = !msg.active || msg.mode !== 'lines';
            $('stop-test').hidden = !msg.active;
            break;
        case 'led':
            showLeds(msg.m);
            break;
        case 'clock':
            state.clock = { epoch: msg.epoch, tzo: msg.tzo || 0, at: performance.now() };
            showTimes();
            break;
        case 'call':
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
    if (state.edit && message !== reportedSize) {
        clearTimeout(sizeTimer);
        sizeTimer = setTimeout(() => {
            if (send(message)) reportedSize = message;
        }, 100);
    }
}

// ---------------------------------------------------------------- transport

let send = () => {};
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
                    const error = new Error('Login unavailable');
                    error.busy = reply.status === 503;
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
            reportedSize = null;
            clearTimeout(sizeTimer);
            $('upload').disabled = true;
            $('line-controls').hidden = true;
            $('stop-test').hidden = true;
            updateControls();
        },
    });
    send = bytes => !state.upload && link.send(bytes);
    reconnectModem = () => link.reconnect();
    openTerminal = () => link.openTerminal();
    closeTerminal = () => link.closeTerminal();
    hangupCall = () => link.hangup();
    pauseInput = busy => link.pause(busy);
    setInterval(() => link.poll(state.upload), 5000);
    link.start();
}

async function startDemo() {
    setLink('demo');
    const { startDemoModem } = await import('./demo.js');
    const modem = startDemoModem({
        message: handleMessage,
        write: data => {
            events.output(typeof data === 'string' ? encoder.encode(data) : data);
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
    hangupCall = () => { modem.receive('{"t":"hangup"}'); return true; };
    term.focus();
}

// ---------------------------------------------------------------- start

async function main() {
    $('open-terminal').onclick = () => {
        state.opening = true;
        updateControls();
        if (!openTerminal()) {
            state.opening = false;
            updateControls();
            toasts.show('Could not open the terminal: the modem link is unavailable');
        }
    };
    $('close-terminal').onclick = () => closeTerminal();
    $('here').onclick = () => { idle.activity(); term.focus(); };
    $('dcd').onclick = () => send(encoder.encode('d'));
    $('ri').onclick = () => send(encoder.encode('r'));
    $('cts').onclick = () => send(encoder.encode('c'));
    $('stop-test').onclick = () => send(new Uint8Array([0x18]));
    $('upload').onclick = () => $('image').click();
    const uploadFlow = createUploadFlow({
        state,
        show: text => { $('operation').textContent = text; },
        setBusy: busy => {
            pauseInput(busy);
            updateControls(false);
            if (!busy && state.edit) requestAnimationFrame(fitTerminal);
        },
        confirm: text => window.confirm(text),
        read: async file => new Uint8Array(await file.arrayBuffer()),
        // Refresh expired Digest credentials with a small request before streaming a large
        // body. This does not replace the current socket.
        login: async () => (await fetch('api/session', {cache: 'no-store'})).ok,
        send: xhrSender(),
        installed: () => {
            toasts.show('Update uploaded — the modem is restarting');
            state.reloadWhenOnline = true;
            reconnectModem();
        },
    });
    $('image').onchange = async () => {
        try {
            const result = await uploadFlow($('image').files[0]);
            if (['rejected', 'lost', 'failed'].includes(result)) toasts.show($('operation').textContent);
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
        if (!send(bytes)) toasts.show(state.edit
            ? 'Input could not be sent. Wait for the modem before pasting again.'
            : 'Open the terminal before typing');
    };
    // xterm maps Enter to CR, Backspace to DEL (Ctrl-H sends BS), and arrows /
    // function keys to ANSI sequences. Preserve those bytes, including paste.
    term.onData(data => input(encoder.encode(data)));
    term.onBinary(data => input(Uint8Array.from(data, c => c.charCodeAt(0) & 0xff)));
    for (const event of ['keydown', 'pointerdown', 'pointermove', 'click', 'wheel']) {
        document.addEventListener(event, () => idle.activity(), {passive: true});
    }
    document.addEventListener('visibilitychange', () => idle.poll());
    setInterval(() => idle.poll(), 1000);
    setInterval(showTimes, 1000);
    showCall();
    showWifi();

    const params = new URLSearchParams(location.search);
    if (params.has('demo') || location.protocol === 'file:' || window.USR_MODEM_DEMO) {
        startDemo();
    } else {
        connectModem();
    }
}

main();
