// Demo modem for previewing the web page without hardware (index.html?demo).
// It imitates the WebSocket messages described in web/PROTOCOL.md. The menu below is a
// mock-up of the AT*CONFIG screen; the real one comes from the firmware's menu model.
//
// Keys: D = simulate a call to phonebook entry 1, H = hang up. No real connection is made.

const ESC = '\x1b[';
const C = {
    reset: `${ESC}0m`,
    blue: `${ESC}0;34m`,
    cyan: `${ESC}1;36m`,
    white: `${ESC}1;37m`,
    grey: `${ESC}0;37m`,
    yellow: `${ESC}1;33m`,
    green: `${ESC}1;32m`,
    red: `${ESC}1;31m`,
    dim: `${ESC}1;30m`,
};

const LOGO = String.raw`
  _   _ ____  ____    __  __           _
 | | | / ___||  _ \  |  \/  | ___   __| | ___ _ __ ___
 | | | \___ \| |_) | | |\/| |/ _ \ / _' |/ _ \ '_ ' _ \
 | |_| |___) |  _ <  | |  | | (_) | (_| |  __/ | | | | |
  \___/|____/|_| \_\ |_|  |_|\___/ \__,_|\___|_| |_| |_|`;

// LED bits, as in status.js (AA = 1 is unused in the demo)
const CD = 2, RD = 4, SD = 8, TR = 16, CS = 32, ARQ = 64;

function menuScreen() {
    const item = (key, label) => `${C.white}[${C.yellow}${key}${C.white}]${C.grey} ${label.padEnd(22)}`;
    const lines = [
        `${ESC}2J${ESC}H${C.cyan}${LOGO.slice(1).replace(/\n/g, '\r\n')}`,
        '',
        `${C.white}  Sportster 33.6 Wi-Fi Emulator   ${C.dim}v0.2  ·  demo  ·  menu mock-up`,
        `${C.blue}  ${'='.repeat(74)}`,
        `${C.grey}  Wi-Fi ${C.green}HomeNet -58 dBm${C.grey}   IP ${C.white}10.0.0.60${C.grey}` +
            `   Line ${C.white}33600${C.grey}   Host ${C.white}38400 8N1`,
        `${C.blue}  ${'='.repeat(74)}`,
        '',
        `    ${item('1', 'Status')}${item('6', 'Sounds')}`,
        `    ${item('2', 'Phone book')}${item('7', 'Client tests')}`,
        `    ${item('3', 'Settings')}${item('8', 'Call log')}`,
        `    ${item('4', 'Wi-Fi')}${item('9', 'Debug & logs')}`,
        `    ${item('5', 'Updates')}${item('A', 'About')}`,
        '',
        `    ${item('D', 'Dial entry 1 (demo)')}${item('H', 'Hang up')}`,
        '',
        `${C.dim}  Demo only: no real connection is made. Menu items are not final.`,
        '',
        `${C.white}  Select: ${C.reset}`,
    ];
    return lines.join('\r\n');
}

export function startDemoModem({ message, write }) {
    const t0 = Date.now() / 1000;
    let leds = TR | CS | ARQ;
    let call = { t: 'call', state: 'idle' };
    let traffic = null;
    let timer = null;

    const now = () => Math.floor(Date.now() / 1000);
    const setLeds = (m) => {
        leds = m;
        message({ t: 'led', m: leds });
    };
    const flash = (bit) => {
        setLeds(leds | bit);
        setTimeout(() => setLeds(leds & ~bit), 60);
    };
    const setCall = (c) => {
        call = { t: 'call', ...c };
        message(call);
    };

    message({ t: 'hello', name: 'usr-modem', ver: '0.2 (demo)' });
    message({ t: 'clock', epoch: Math.floor(t0), tzo: -new Date().getTimezoneOffset() });
    message({ t: 'wifi', state: 'up', ssid: 'HomeNet', rssi: -58, ip: '10.0.0.60' });
    setLeds(leds);
    setCall({ state: 'idle' });
    write(menuScreen());

    const hangUp = () => {
        clearInterval(traffic);
        clearTimeout(timer);
        traffic = null;
        setLeds(leds & ~(CD | RD | SD));
        setCall({ state: 'idle' });
        write(`\r\n\r\n${C.white}NO CARRIER${C.reset}\r\n`);
        timer = setTimeout(() => write(menuScreen()), 1500);
    };

    const dial = () => {
        if (call.state !== 'idle') {
            return;
        }
        const entry = { number: '1', host: 'www.amigaretro.com', ip: '170.64.142.64', port: 23 };
        setCall({ state: 'dialling', ...entry });
        write(`${ESC}2J${ESC}H${C.grey}ATDT1\r\n${C.dim}(dial tone, DTMF, handshake...)${C.reset}`);
        flash(SD);
        timer = setTimeout(() => {
            setCall({ state: 'connected', mode: 'telnet', dir: 'out', speed: 33600, since: now(), ...entry });
            setLeds(leds | CD);
            write(`\r\n${C.white}CONNECT 33600${C.reset}\r\n\r\n` +
                `${C.dim}[demo] Simulated call: the RD and SD lights show made-up traffic.\r\n` +
                `[demo] Press H to hang up.${C.reset}\r\n`);
            traffic = setInterval(() => {
                if (Math.random() < 0.7) flash(RD);
                if (Math.random() < 0.15) flash(SD);
            }, 180);
        }, 3500);
    };

    return {
        receive(bytes) {
            const text = new TextDecoder().decode(bytes);
            for (const ch of text) {
                flash(SD);
                const key = ch.toUpperCase();
                if (key === 'D') {
                    dial();
                } else if (key === 'H' && call.state !== 'idle') {
                    hangUp();
                } else if (call.state === 'idle' && ch >= ' ') {
                    write(`${ch}\r\n${C.dim}  Not available in the demo.${C.white}\r\n  Select: ${C.reset}`);
                }
            }
        },
    };
}
