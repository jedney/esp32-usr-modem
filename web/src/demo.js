// Synthetic list preview. Production output comes from the C++ terminal.
import data from './ui-data.js';
import commands from './ui-commands.js';

export function startDemoModem({message, write}) {
    const metadata = new Map(commands.map(item => [item.id, item]));
    let screen = null, detail = null, swallowEnter = false, skipLf = false, escape = '';
    const screens = [];
    let opened = false, menu = false, call = false, onlineCommand = false, line = '', cols = 80, rows = 25;
    let pluses = 0;
    let received = 0, waiting = null;
    const book = new Map([['1', '1,bbs.example.net,23,TELNET,0'], ['2', '2,retro.example.org,2323,TELNET,0'],
        ['3', '3,ssh.example.org,22,SSH,0']]);
    const ready = () => message({t:'ready', idle:opened && !menu && !call && !waiting && !line, rx:received});
    const access = () => message({t: 'access', edit: opened, state: opened ? 'terminal' : 'read-only'});
    const hangup = () => {
        call = false;
        onlineCommand = false;
        pluses = 0;
        line = '';
        message({t: 'call', state: 'idle', browser: false});
    };
    const redraw = () => {
        let out = '\r\n\x1b[1;36mSportster DEMO - ' + (screen?.label || 'Configuration') + '\x1b[0m\r\n';
        out += 'DEMO: synthetic preview; no device services connected.\r\n';
        if (detail) out += detail + '\r\nX. Back\r\n';
        else {
            for (const item of screen ? screen.items : data.menu) {
                const m = metadata.get(item.id);
                out += `${item.key}. ${m.friendly}\r\n`;
            }
            out += screen ? 'X. Back\r\n' : 'Q. Exit preview\r\n';
        }
        write(out);
    };
    const back = () => {
        if (detail) detail = null;
        else screen = screens.pop() || null;
        redraw();
    };
    const key = ch => {
        if ((ch === '\r' || ch === '\n') && swallowEnter) { swallowEnter = false; return; }
        const upper = ch.toUpperCase();
        if (upper === 'Q' && !screen && !detail) { menu = false; write('\r\nOK\r\n'); return; }
        if (ch === '\x1b' || upper === 'X' || upper === 'Q') { back(); return; }
        if (ch === '\x0c') { redraw(); return; }
        if (detail) { if (ch === '\r' || ch === '\n') back(); return; }
        const item = (screen ? screen.items : data.menu).find(i => i.key === upper);
        if (!item) return;
        const m = metadata.get(item.id);
        if (item.items?.length) { screens.push(screen); screen = item; }
        else if (screen?.id === 'client-tests') detail = 'Client Tests run on serial only.';
        else {
            detail = `DEMO: ${m.friendly}\r\n${m.url || 'No device service connected.'}`;
            for (const [i, value] of (m.values || []).slice(0, 15).entries())
                detail += `\r\n${String.fromCharCode(65 + i)}. ${value.label}`;
        }
        swallowEnter = true;
        redraw();
    };
    message({t: 'hello', name: 'Sportster DEMO', ver: 'DEMO'});
    message({t: 'clock', epoch: Math.floor(Date.now() / 1000), tzo: 0});
    message({t: 'wifi', state: 'up', ssid:'Demo Wi-Fi', rssi:-48}); message({t: 'led', m: 112}); message({t: 'call', state: 'idle', model:'US Robotics Sportster 14.4k'});
    access();
    const command = () => {
        const original = line.trim();
        const text = line.trim().toUpperCase();
        line = '';
        if (text === 'AT*CONFIG') { menu = true; redraw(); }
        else if (/^AT(?:D|\*DIAL=)/.test(text)) {
            call = true;
            onlineCommand = false;
            message({t: 'call', state: 'connected', mode: 'telnet', browser: true,
                host: 'Synthetic DEMO BBS', number: text.replace(/^AT(?:DT?|\*DIAL=)/, ''), speed: 14400});
            write('\r\nCONNECT 14400\r\n\x1b[1;36mSynthetic DEMO BBS\x1b[0m\r\n'
                + 'Your keys are echoed here. No network call is made.\r\n'
                + 'Type +++ on its own, then ATH to hang up.\r\n');
        } else if (text === 'AT*SIZE?') write(`\r\n${cols},${rows}\r\nOK\r\n`);
        else if (/^AT\*MODEL=(14400|28800|33600|56000)$/.test(text)) {
            const selected = commands.find(c => c.id === 'settings.model').values.find(v => v.value === text.split('=')[1]);
            message({t:'call',state:'idle',model:selected.label});
            write('\r\nDEMO: synthetic command accepted; no device settings changed.\r\nOK\r\n');
        }
        else if (text === 'AT*HELP') write('\r\nDEMO: AT*CONFIG, ATDT1, ATH. No device services connected.\r\nOK\r\n');
        else if (text === 'ATH') { hangup(); write('\r\nOK\r\n'); }
        else if (text === 'ATO' && call) { onlineCommand = false; write('\r\nCONNECT 14400\r\n'); }
        else if (text === 'AT*PHONEBOOK?') write('\r\n' + [...book.values()].join('\r\n') + '\r\nOK\r\n');
        else if (text.startsWith('AT*PHONEBOOK=')) {
            const entry = original.slice('AT*PHONEBOOK='.length);
            book.set(entry.split(',')[0], entry.split(',').length === 4 ? entry + ',0' : entry);
            write('\r\nDEMO: destination saved for this preview only.\r\nOK\r\n');
        } else if (text.startsWith('AT*PHONEDELETE=')) {
            book.delete(original.split('=')[1]); write('\r\nOK\r\n');
        } else if (text === 'AT*WIFISCAN') {
            waiting = 'key';
            write('\r\n 1  Demo Wi-Fi                         -48 dBm  ch  1  WPA2\r\n'
                + ' 2  Guest network                       -73 dBm  ch  6  Open\r\nPress a key to continue.\r\n');
        } else if (text.startsWith('AT*WIFIJOIN=')) {
            waiting = 'password'; write('\r\nDEMO: no network will be joined.\r\nWi-Fi password: ');
        } else if (/^AT\*(?:RESTART|FACTORY|WIFI=OFF|WIFIFORGET)$/.test(text)) {
            waiting = 'confirm'; write('\r\nDEMO: confirm in the terminal (Y/N, then Enter): ');
        } else if (/^AT\*(?:PLAY=|HW)/.test(text)) {
            waiting = text.startsWith('AT*HWBRIDGE') ? 'bridge' : 'key';
            message({t:'hardware',active:true,mode:text === 'AT*HWLINES' ? 'lines' : ''});
            write('\r\nDEMO: no hardware is operated. ' + (waiting === 'bridge' ? 'Press Ctrl-X to stop.' : 'Press any key to finish.') + '\r\n');
        } else if (commands.some(c => c.template && text.startsWith('AT' + c.template.split(/[={]/)[0].toUpperCase()))) {
            write('\r\nDEMO: synthetic command accepted; no device settings changed.\r\nOK\r\n');
        }
        else write(text === 'AT' ? '\r\nOK\r\n' : '\r\nERROR\r\n');
    };
    const close = () => {
        hangup();
        opened = false; menu = false; screen = null; detail = null;
        screens.length = 0;
        waiting = null; received = 0;
        swallowEnter = false; skipLf = false; escape = '';
        access();
    };
    return {
        close,
        receive(bytes) {
            if (typeof bytes === 'string') {
                const msg = JSON.parse(bytes);
                if (msg.t === 'open') {
                    received = 0;
                    opened = true; access();
                    write('AT\r\r\nOK\r\n');
                } else if (opened && msg.t === 'hangup') { received = 0; hangup(); }
                else if (opened && msg.t === 'size') { cols = msg.cols; rows = msg.rows; }
                ready(); return;
            }
            if (!opened) return;
            received = (received + bytes.length) >>> 0;
            if (call && !onlineCommand) {
                for (const ch of new TextDecoder().decode(bytes)) {
                    if (ch === '+') {
                        if (++pluses === 3) { pluses = 0; onlineCommand = true; write('\r\nOK\r\n'); }
                    } else {
                        write('+'.repeat(pluses) + ch);
                        pluses = 0;
                    }
                }
                ready(); return;
            }
            for (const ch of new TextDecoder().decode(bytes)) {
                if (ch === '\n' && skipLf) { skipLf = false; continue; }
                skipLf = ch === '\r';
                if (waiting) {
                    const finish = waiting === 'key' || (waiting === 'bridge' ? ch === '\x18' : ch === '\r' || ch === '\n' || ch === '\x1b');
                    if (finish) {
                        waiting = null; message({t:'hardware',active:false}); write('\r\nDEMO: finished.\r\nOK\r\n');
                    } else if (waiting === 'password' && ch >= ' ') write('*');
                    continue;
                }
                if (!menu) {
                    if (ch === '\r' || ch === '\n') command();
                    else if (ch === '\x08' || ch === '\x7f') { line = line.slice(0, -1); write('\b \b'); }
                    else if (ch >= ' ') { line = (line + ch).slice(0, 256); write(ch); }
                    continue;
                }
                if (escape) {
                    escape += ch;
                    if (escape.length === 2 && !['[', 'O'].includes(ch)) escape = '';
                    else if (escape.length > 2 && /[A-Za-z~]$/.test(ch)) escape = '';
                } else if (ch === '\x1b') {
                    escape = ch;
                    queueMicrotask(() => { if (escape === '\x1b') { escape = ''; key('\x1b'); } });
                } else key(ch);
            }
            ready();
        },
    };
}
