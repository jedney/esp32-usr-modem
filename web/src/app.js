// USR Modem web interface: an 80 x 25 terminal showing the modem's configuration screen,
// plus a status bar with the live front-panel lights, call state and modem clock.
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
    csrf: null, version: null, edit: false, upload: false, sizeSupported: false,
    reloadWhenOnline: false, // set after a successful update: reload once the modem is back
};

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
    document.body.classList.toggle('online', status === 'online');
    document.body.classList.toggle('offline', status === 'offline');
    $('link-label').textContent =
        { online: 'Online', offline: 'Offline', connecting: 'Connecting', demo: 'Demo' }[status];
    if (status === 'demo') {
        document.body.classList.add('online');
    }
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
            state.sizeSupported = msg.size === true;
            requestAnimationFrame(fitTerminal);
            break;
        case 'access':
            state.edit = msg.edit === true;
            $('upload').disabled = !state.edit || state.upload;
            $('enter').disabled = state.edit;
            $('operation').textContent = state.edit ? 'Configuration active' : 'Read-only live status';
            if (state.edit) requestAnimationFrame(fitTerminal);
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
            state.call = msg;
            showCall();
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
    scrollback: 0,
    drawBoldTextInBrightColors: true,
    allowTransparency: false,
});

let sizeTimer = null;
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
    const geometry = state.sizeSupported ? terminalGeometry(width, height, state.cell.w * 16, state.cell.h * 16) : {cols: COLS, rows: ROWS};
    if (term.cols !== geometry.cols || term.rows !== geometry.rows) term.resize(geometry.cols, geometry.rows);
    const size = S.fitFontSize(width, height, term.cols, term.rows, state.cell.w, state.cell.h);
    if (size !== term.options.fontSize) {
        term.options.fontSize = size;
    }
    clearTimeout(sizeTimer);
    if (state.sizeSupported && state.edit) sizeTimer = setTimeout(() => send(sizeMessage(term.cols, term.rows)), 100);
}

// ---------------------------------------------------------------- transport

let send = () => {};
let reconnectModem = () => {};

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
            else term.write(new Uint8Array(data));
        },
        status: status => {
            setLink(status);
            if (status !== 'online') return;
            if (state.reloadWhenOnline) location.reload(); // new firmware may bring new web files
            term.focus();
        },
        disconnect: () => {
            state.csrf = null;
            state.edit = false;
            state.sizeSupported = false;
            clearTimeout(sizeTimer);
            $('upload').disabled = true;
            $('line-controls').hidden = true;
            $('stop-test').hidden = true;
        },
    });
    send = bytes => !state.upload && link.send(bytes);
    reconnectModem = () => link.reconnect();
    setInterval(() => link.poll(state.upload), 5000);
    link.start();
}

async function startDemo() {
    setLink('demo');
    $('enter').disabled = true;
    $('operation').textContent = 'DEMO - synthetic values; Phonebook 4 simulates a call';
    state.edit = true;
    const { startDemoModem } = await import('./demo.js');
    const modem = startDemoModem({
        message: handleMessage,
        write: (data) => term.write(data),
    });
    send = (bytes) => modem.receive(bytes);
    term.focus();
}

// ---------------------------------------------------------------- start

async function main() {
    $('enter').onclick = () => send(encoder.encode('e\r'));
    $('dcd').onclick = () => send(encoder.encode('d'));
    $('ri').onclick = () => send(encoder.encode('r'));
    $('cts').onclick = () => send(encoder.encode('c'));
    $('stop-test').onclick = () => send(new Uint8Array([0x18]));
    $('upload').onclick = () => $('image').click();
    const uploadFlow = createUploadFlow({
        state,
        show: text => { $('operation').textContent = text; },
        setBusy: busy => { $('upload').disabled = busy || !state.edit; },
        confirm: text => window.confirm(text),
        read: async file => new Uint8Array(await file.arrayBuffer()),
        // Refresh expired Digest credentials with a small request before streaming a large
        // body. This does not replace the current socket.
        login: async () => (await fetch('api/session', {cache: 'no-store'})).ok,
        send: xhrSender(),
        installed: () => { state.reloadWhenOnline = true; reconnectModem(); },
    });
    $('image').onchange = async () => {
        try { await uploadFlow($('image').files[0]); } finally { $('image').value = ''; }
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
    term.onData((data) => send(encoder.encode(data)));
    term.onBinary((data) => send(Uint8Array.from(data, (c) => c.charCodeAt(0) & 0xff)));
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
