import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

class Element {
    children = []; attributes = {}; dataset = {}; hidden = false; disabled = false; textContent = '';
    clientWidth = 1600; clientHeight = 960;
    classList = {toggle() {}, add() {}};
    setAttribute(k, v) { this.attributes[k] = v; }
    appendChild(el) { el.parent = this; this.children.push(el); }
    remove() { this.parent.children.splice(this.parent.children.indexOf(this), 1); }
    querySelector() { return {offsetWidth: 960, offsetHeight: 500}; }
}

test('page wires ownership, keys, paste, terminal-mode resize, idle hangup and close', async t => {
    const elements = new Map(), listeners = new Map(), intervals = [], timers = new Map(), sockets = [];
    let time = 0, term, menuOptions;
    const element = id => {
        if (!elements.has(id)) elements.set(id, new Element());
        return elements.get(id);
    };
    class Terminal {
        element = new Element(); writes = [];
        constructor(options) { this.options = options; this.cols = options.cols; this.rows = options.rows; term = this; }
        open() {} focus() {} resize(cols, rows) { this.cols = cols; this.rows = rows; }
        write(bytes) { this.writes.push(bytes); }
        onData(fn) { this.data = fn; } onBinary(fn) { this.binary = fn; }
    }
    class Socket {
        readyState = 0; bufferedAmount = 0; sent = [];
        constructor() { sockets.push(this); }
        send(data) { this.sent.push(data); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; this.onopen(); }
        message(msg) { this.onmessage({data: JSON.stringify(msg)}); }
    }
    const globals = {
        document: {getElementById: element, createElement: () => new Element(), body: new Element(),
            fonts: {load: async () => {}}, addEventListener: (name, fn) => listeners.set(name, fn)},
        window: {addEventListener: (name, fn) => listeners.set(name, fn)},
        location: {host: 'modem.local', protocol: 'http:', search: ''},
        getComputedStyle: () => ({paddingLeft: '0', paddingRight: '0', paddingTop: '0', paddingBottom: '0'}),
        requestAnimationFrame: fn => fn(), WebSocket: Socket,
        fetch: async () => ({ok: true, json: async () => ({token: 'ticket'})}),
        setTimeout: (fn, ms) => { timers.set(fn, ms); return fn; },
        clearTimeout: fn => timers.delete(fn),
        setInterval: (fn, ms) => { intervals.push({fn, ms}); return fn; },
        __TestTerminal: Terminal,
        __TestCreateMenus: options => { menuOptions = options; return {update() {}}; },
    };
    for (const [name, value] of Object.entries(globals)) {
        const descriptor = Object.getOwnPropertyDescriptor(globalThis, name);
        Object.defineProperty(globalThis, name, {value, configurable: true, writable: true});
        t.after(() => descriptor ? Object.defineProperty(globalThis, name, descriptor) : delete globalThis[name]);
    }
    t.mock.method(Date, 'now', () => time);
    let source = await readFile(new URL('../src/app.js', import.meta.url), 'utf8');
    source = source.replace("import { Terminal } from './xterm.mjs';", 'const Terminal = globalThis.__TestTerminal;');
    source = source.replace("import {createMenuBar} from './menu-view.js';", 'const createMenuBar = globalThis.__TestCreateMenus;');
    source = source.replace(/from '(\.\/[^']+)'/g,
        (_, name) => `from '${new URL('../src/' + name.slice(2), import.meta.url).href}'`);
    source = source.replace(/main\(\);\s*$/, 'await main();');
    source += '\n//# sourceURL=app-under-test.js';
    await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
    for (let n = 0; n < 10; n++) await Promise.resolve();
    const ws = sockets[0]; ws.open();
    assert.equal(term.options.disableStdin, true);
    element('open-terminal').onclick();
    assert.deepEqual(ws.sent, ['{"t":"open"}']);
    ws.message({t: 'access', edit: true, state: 'terminal'});
    assert.equal(term.options.disableStdin, false);
    assert.equal(element('close-terminal').disabled, false);
    assert.equal(menuOptions.state().busy, true);
    ws.message({t:'ready', idle:true, rx:0});
    assert.equal(menuOptions.state().busy, false);
    assert.equal(menuOptions.run('AT*MODEL=28800\r'), true);
    assert.deepEqual(ws.sent.at(-1), new TextEncoder().encode('AT*MODEL=28800\r'));
    assert.equal(menuOptions.state().busy, true);
    ws.message({t:'ready', idle:true, rx:15});
    assert.equal(menuOptions.state().busy, false);
    const runTimers = ms => {
        for (const [fn, delay] of [...timers]) if (delay === ms) { timers.delete(fn); fn(); }
    };
    runTimers(100);
    assert.ok(term.cols > 132 && term.rows > 50);
    assert.ok(ws.sent.includes(JSON.stringify({t: 'size', cols: term.cols, rows: term.rows})));
    element('screen').clientWidth = 3000;
    listeners.get('resize')(); runTimers(100);
    assert.equal(JSON.parse(ws.sent.at(-1)).cols, term.cols);
    const keys = '\x08\x7f\r\x1b[A\x1b[B\x1b[C\x1b[D\x1bOP\x1b[15~';
    term.data(keys);
    assert.deepEqual(ws.sent.at(-1), new TextEncoder().encode(keys));
    term.binary('\xff'); assert.deepEqual(ws.sent.at(-1), new Uint8Array([255]));
    term.data('x'.repeat(1600));
    while ([...timers.values()].includes(10)) runTimers(10);
    assert.ok(ws.sent.filter(s => s instanceof Uint8Array).every(s => s.length <= 512));
    // No call and a serial call cannot arm the idle guard, even while this terminal is open.
    const pollIdle = intervals.find(i => i.ms === 1000 && !i.fn.name).fn;
    time = 900000; pollIdle();
    assert.ok(!ws.sent.includes('{"t":"hangup"}'));
    ws.message({t: 'call', state: 'connected', browser: false});
    time = 1800000; pollIdle(); assert.ok(!ws.sent.includes('{"t":"hangup"}'));
    ws.message({t: 'call', state: 'connected', browser: true, host: 'Demo BBS'});
    time += 240000; pollIdle(); assert.equal(element('idle-guard').hidden, false);
    listeners.get('pointermove')(); assert.equal(element('idle-guard').hidden, true);
    time += 240000; pollIdle(); assert.equal(element('idle-guard').hidden, false);
    // BBS output is deliberately absent from the activity sources.
    ws.onmessage({data: new TextEncoder().encode('Still here from the BBS').buffer});
    time += 60000; pollIdle();
    assert.equal(ws.sent.at(-1), '{"t":"hangup"}');
    assert.equal(term.options.disableStdin, false);
    ws.message({t: 'call', state: 'idle', browser: false});
    assert.ok(element('toasts').children.some(el => el.textContent.includes('because you were away')));
    await element('close-terminal').onclick();
    assert.equal(ws.readyState, 3);
    assert.equal(term.options.disableStdin, true);
    assert.ok(element('toasts').children.some(el => el.textContent === 'Terminal closed'));
});
