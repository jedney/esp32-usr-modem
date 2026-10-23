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
            document.title = `${msg.name || 'USR Modem'} v${msg.ver || '?'}`;
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

// Scale the font so the fixed 80 x 25 screen fills the space above the status bar.
function fitTerminal() {
    const screen = $('screen');
    const style = getComputedStyle(screen);
    const width = screen.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
    const height = screen.clientHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom);
    if (!state.cell) {
        const el = term.element.querySelector('.xterm-screen');
        const size = term.options.fontSize;
        state.cell = { w: el.offsetWidth / COLS / size, h: el.offsetHeight / ROWS / size };
    }
    const size = S.fitFontSize(width, height, COLS, ROWS, state.cell.w, state.cell.h);
    if (size !== term.options.fontSize) {
        term.options.fontSize = size;
    }
}

// ---------------------------------------------------------------- transport

let send = () => {};

function connectModem() {
    let attempt = 0;
    const open = async () => {
        setLink('connecting');
        try {
            // The page itself is behind Digest authentication; this returns a short-lived
            // token for the WebSocket, which can't carry Digest credentials.
            const reply = await fetch('api/session', { cache: 'no-store' });
            if (!reply.ok) {
                throw new Error(`session ${reply.status}`);
            }
            const { token } = await reply.json();
            const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
            const ws = new WebSocket(`${scheme}://${location.host}/ws?token=${encodeURIComponent(token)}`);
            ws.binaryType = 'arraybuffer';
            ws.onopen = () => {
                attempt = 0;
                setLink('online');
                send = (bytes) => ws.readyState === WebSocket.OPEN && ws.send(bytes);
                term.focus();
            };
            ws.onmessage = (ev) => {
                if (typeof ev.data === 'string') {
                    handleMessage(S.parseMessage(ev.data));
                } else {
                    term.write(new Uint8Array(ev.data));
                }
            };
            ws.onclose = () => {
                send = () => {};
                retry();
            };
        } catch {
            retry();
        }
    };
    const retry = () => {
        setLink('offline');
        setTimeout(open, S.reconnectDelay(attempt++));
    };
    open();
}

async function startDemo() {
    setLink('demo');
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
