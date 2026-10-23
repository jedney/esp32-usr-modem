// Pure helpers for the status bar. No DOM access, so they are unit-tested with
// `node --test web/test` (see web/test/status.test.mjs).

// Front-panel LEDs in panel order. Bit n of the LED mask is LEDS[n].
export const LEDS = [
    { id: 'AA', title: 'Auto Answer' },
    { id: 'CD', title: 'Carrier Detect' },
    { id: 'RD', title: 'Received Data' },
    { id: 'SD', title: 'Send Data' },
    { id: 'TR', title: 'Terminal Ready (DTR)' },
    { id: 'CS', title: 'Clear To Send' },
    { id: 'ARQ', title: 'ARQ/FAX: Wi-Fi status' },
];

/** Decode an LED bit mask into { AA: bool, CD: bool, ... }. */
export function decodeLeds(mask) {
    const m = Number.isInteger(mask) ? mask : 0;
    const out = {};
    LEDS.forEach((led, bit) => {
        out[led.id] = ((m >> bit) & 1) === 1;
    });
    return out;
}

const pad2 = (n) => String(n).padStart(2, '0');

/** Seconds -> "hh:mm:ss", or "Nd hh:mm:ss" from one day. Negative/invalid -> "--:--:--". */
export function formatDuration(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0) {
        return '--:--:--';
    }
    const s = Math.floor(seconds);
    const days = Math.floor(s / 86400);
    const hh = pad2(Math.floor((s % 86400) / 3600));
    const mm = pad2(Math.floor((s % 3600) / 60));
    const ss = pad2(s % 60);
    return days > 0 ? `${days}d ${hh}:${mm}:${ss}` : `${hh}:${mm}:${ss}`;
}

const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

/**
 * Modem clock -> "Fri 23 Oct 22:27:05".
 * epoch: UTC seconds; tzoMinutes: the modem's UTC offset in minutes (e.g. 780 for NZDT).
 * Uses the modem's time zone, not the browser's.
 */
export function formatClock(epoch, tzoMinutes) {
    if (!Number.isFinite(epoch) || epoch <= 0) {
        return 'clock not set';
    }
    const d = new Date((Math.floor(epoch) + (tzoMinutes || 0) * 60) * 1000);
    return `${DAYS[d.getUTCDay()]} ${d.getUTCDate()} ${MONTHS[d.getUTCMonth()]} ` +
        `${pad2(d.getUTCHours())}:${pad2(d.getUTCMinutes())}:${pad2(d.getUTCSeconds())}`;
}

const STATES = {
    idle: { label: 'Idle', cls: 'idle' },
    dialling: { label: 'Dialling', cls: 'busy' },
    ringing: { label: 'Ringing', cls: 'busy' },
    connected: { label: 'Connected', cls: 'up' },
    ppp: { label: 'Internet (PPP)', cls: 'up' },
    slip: { label: 'Internet (SLIP)', cls: 'up' },
    config: { label: 'Config screen', cls: 'idle' },
    test: { label: 'Client test', cls: 'busy' },
};

const MODES = { telnet: 'Telnet', raw: 'Raw' };

/** Call info -> { label, cls } for the connection-status item. */
export function describeCall(call) {
    const base = STATES[call && call.state] || { label: 'Unknown', cls: 'idle' };
    if (!call || call.state !== 'connected') {
        return { ...base };
    }
    const parts = [base.label];
    if (call.dir === 'in') {
        parts[0] = 'Connected (incoming)';
    }
    if (MODES[call.mode]) {
        parts.push(MODES[call.mode]);
    }
    if (Number.isInteger(call.speed) && call.speed > 0) {
        parts.push(String(call.speed));
    }
    return { label: parts.join(' · '), cls: base.cls };
}

/**
 * Remote end -> "www.amigaretro.com (170.64.142.64:23)", or "170.64.142.64:23" when the host
 * is already an IP address, or "—" when there is no remote end.
 */
export function describeRemote(call) {
    if (!call || !['dialling', 'ringing', 'connected', 'ppp', 'slip'].includes(call.state)) {
        return '—';
    }
    const host = call.host || '';
    const ip = call.ip || '';
    const port = Number.isInteger(call.port) ? `:${call.port}` : '';
    if (!host && !ip) {
        return '—';
    }
    if (host && ip && host !== ip) {
        return `${host} (${ip}${port})`;
    }
    return `${host || ip}${port}`;
}

/** Wi-Fi RSSI (dBm) -> signal bars 0..4. */
export function rssiBars(rssi) {
    if (!Number.isFinite(rssi) || rssi >= 0) {
        return 0;
    }
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

/** Seconds the current call has been up, from the modem clock. null when not connected. */
export function callUptime(call, nowEpoch) {
    if (!call || !Number.isFinite(call.since) || call.since <= 0) {
        return null;
    }
    if (!['connected', 'ppp', 'slip'].includes(call.state)) {
        return null;
    }
    return Math.max(0, nowEpoch - call.since);
}

/** Reconnect delay in ms for attempt 0, 1, 2, ... (1 s, 2 s, 5 s, then 10 s). */
export function reconnectDelay(attempt) {
    const steps = [1000, 2000, 5000];
    return attempt < steps.length ? steps[Math.max(0, attempt)] : 10000;
}

/**
 * Font size (px) that fits a cols x rows terminal into width x height.
 * charRatio: character width / font size for the font; lineHeight: xterm lineHeight option.
 * Clamped to 8..40 px.
 */
export function fitFontSize(width, height, cols, rows, charRatio, lineHeight) {
    if (!(width > 0 && height > 0 && cols > 0 && rows > 0 && charRatio > 0 && lineHeight > 0)) {
        return 16;
    }
    const byWidth = width / (cols * charRatio);
    const byHeight = height / (rows * lineHeight);
    return Math.max(8, Math.min(40, Math.floor(Math.min(byWidth, byHeight))));
}

/** Parse a text frame from the modem. Returns the object, or null if it isn't a valid message. */
export function parseMessage(text) {
    try {
        const msg = JSON.parse(text);
        return msg && typeof msg === 'object' && typeof msg.t === 'string' ? msg : null;
    } catch {
        return null;
    }
}
