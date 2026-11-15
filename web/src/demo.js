// Synthetic list preview. Production output comes from the C++ terminal.
import data from './ui-data.js';
import commands from './ui-commands.js';

export function startDemoModem({message, write}) {
    const metadata = new Map(commands.map(item => [item.id, item]));
    let screen = null, detail = null, swallowEnter = false, skipLf = false, escape = '';
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
    redraw();
    return {
        receive(bytes) {
            // Size belongs to the transport. A list needs no resize or redraw.
            if (typeof bytes === 'string') return;
            for (const ch of new TextDecoder().decode(bytes)) {
                if (ch === '\n' && skipLf) { skipLf = false; continue; }
                skipLf = ch === '\r';
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
