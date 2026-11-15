// Synthetic list preview. Production output comes from the C++ terminal.
import data from './ui-data.js';
import commands from './ui-commands.js';

export function startDemoModem({message, write}) {
    const metadata = new Map(commands.map(item => [item.id, item]));
    let screen = null, detail = null, swallowEnter = false, skipLf = false, escape = '';
    let opened = false, menu = false, call = false, onlineCommand = false, line = '', cols = 80, rows = 25;
    let pluses = 0;
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
                out += `${item.key}. ${m.friendly}\r\n   ${m.help}\r\n`;
            }
            out += screen ? 'X. Back\r\n' : 'Q. Exit preview\r\n';
        }
        write(out);
    };
    const back = () => {
        if (detail) detail = null;
        else screen = null;
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
        if (!screen && item.items.length) screen = item;
        else if (screen?.id === 'client-tests') detail = 'Client Tests run on serial only.';
        else {
            detail = `DEMO: ${m.friendly}\r\n${m.help}\r\nNo device service connected.`;
            for (const [i, value] of (m.values || []).slice(0, 15).entries())
                detail += `\r\n${String.fromCharCode(65 + i)}. ${value.label}`;
        }
        swallowEnter = true;
        redraw();
    };
    message({t: 'hello', name: 'Sportster DEMO', ver: 'DEMO'});
    message({t: 'clock', epoch: 0, tzo: 0});
    message({t: 'wifi', state: 'down'}); message({t: 'led', m: 0}); message({t: 'call', state: 'idle'});
    access();
    const command = () => {
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
        else if (text === 'AT*HELP') write('\r\nDEMO: AT*CONFIG, ATDT1, ATH. No device services connected.\r\nOK\r\n');
        else if (text === 'ATH') { hangup(); write('\r\nOK\r\n'); }
        else if (text === 'ATO' && call) { onlineCommand = false; write('\r\nCONNECT 14400\r\n'); }
        else write(text === 'AT' ? '\r\nOK\r\n' : '\r\nERROR\r\n');
    };
    const close = () => {
        hangup();
        opened = false; menu = false; screen = null; detail = null;
        swallowEnter = false; skipLf = false; escape = '';
        access();
    };
    return {
        close,
        receive(bytes) {
            if (typeof bytes === 'string') {
                const msg = JSON.parse(bytes);
                if (msg.t === 'open') {
                    opened = true; access();
                    write('Sportster DEMO terminal — synthetic values only.\r\n'
                        + 'Type AT*CONFIG for the list preview, or ATDT1 for a simulated call.\r\n');
                } else if (opened && msg.t === 'hangup') hangup();
                else if (opened && msg.t === 'size') { cols = msg.cols; rows = msg.rows; }
                return;
            }
            if (!opened) return;
            if (call && !onlineCommand) {
                for (const ch of new TextDecoder().decode(bytes)) {
                    if (ch === '+') {
                        if (++pluses === 3) { pluses = 0; onlineCommand = true; write('\r\nOK\r\n'); }
                    } else {
                        write('+'.repeat(pluses) + ch);
                        pluses = 0;
                    }
                }
                return;
            }
            for (const ch of new TextDecoder().decode(bytes)) {
                if (ch === '\n' && skipLf) { skipLf = false; continue; }
                skipLf = ch === '\r';
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
        },
    };
}
