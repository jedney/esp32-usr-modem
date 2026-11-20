// Loads the real app.js against hand-made browser fakes. respond(path, options) answers fetch().
import {readFile} from 'node:fs/promises';

export class Element {
    children = []; attributes = {}; dataset = {}; hidden = false; disabled = false; textContent = '';
    clientWidth = 1600; clientHeight = 960; classes = new Set();
    classList = {toggle: () => {}, add: name => this.classes.add(name)};
    setAttribute(k, v) { this.attributes[k] = v; }
    appendChild(el) { el.parent = this; this.children.push(el); }
    remove() { this.parent.children.splice(this.parent.children.indexOf(this), 1); }
    querySelector() { return {offsetWidth: 960, offsetHeight: 500}; }
}

export const ok = (json = {token: 'ticket'}, text = '') =>
    ({ok: true, status: 200, json: async () => json, text: async () => text});
export const status = code => ({ok: false, status: code, json: async () => ({}), text: async () => ''});
export const AUTH = {user: 'admin', expires: 43200, csrf: 'c'.repeat(32)};

export async function loadApp(t, respond) {
    const app = {elements: new Map(), listeners: new Map(), intervals: [], timers: new Map(), sockets: [],
        fetches: [], replaced: [], menuClears: 0, time: 0, term: null, menuOptions: null};
    const element = id => {
        if (!app.elements.has(id)) app.elements.set(id, new Element());
        return app.elements.get(id);
    };
    app.element = element;
    class Terminal {
        element = new Element(); writes = []; resets = 0;
        constructor(options) { this.options = options; this.cols = options.cols; this.rows = options.rows; app.term = this; }
        open() {} focus() {} resize(cols, rows) { this.cols = cols; this.rows = rows; }
        write(bytes) { this.writes.push(bytes); }
        clear() { this.writes = []; }
        reset() { this.writes = []; this.resets++; }
        onData(fn) { this.data = fn; } onBinary(fn) { this.binary = fn; }
    }
    class Socket {
        readyState = 0; bufferedAmount = 0; sent = [];
        constructor() { app.sockets.push(this); }
        send(data) { this.sent.push(data); }
        close() { this.readyState = 3; }
        open() { this.readyState = 1; this.onopen(); }
        message(msg) { this.onmessage({data: JSON.stringify(msg)}); }
    }
    const document = {getElementById: element, createElement: () => new Element(), body: new Element(),
        hidden: false, title: '',
        fonts: {load: async () => {}}, addEventListener: (name, fn) => app.listeners.set(name, fn)};
    app.document = document;
    const globals = {
        document,
        window: {addEventListener: (name, fn) => app.listeners.set(name, fn)},
        location: {host: 'modem.local', protocol: 'http:', search: '', replace: url => app.replaced.push(url)},
        getComputedStyle: () => ({paddingLeft: '0', paddingRight: '0', paddingTop: '0', paddingBottom: '0'}),
        requestAnimationFrame: fn => fn(), WebSocket: Socket,
        fetch: async (path, options) => {
            app.fetches.push({path, options});
            return respond(path, options);
        },
        setTimeout: (fn, ms) => { app.timers.set(fn, ms); return fn; },
        clearTimeout: fn => app.timers.delete(fn),
        setInterval: (fn, ms) => { app.intervals.push({fn, ms}); return fn; },
        __TestTerminal: Terminal,
        __TestCreateMenus: options => {
            app.menuOptions = options;
            return {update() {}, clear() { app.menuClears++; }};
        },
    };
    for (const [name, value] of Object.entries(globals)) {
        const descriptor = Object.getOwnPropertyDescriptor(globalThis, name);
        Object.defineProperty(globalThis, name, {value, configurable: true, writable: true});
        t.after(() => descriptor ? Object.defineProperty(globalThis, name, descriptor) : delete globalThis[name]);
    }
    t.mock.method(Date, 'now', () => app.time);
    app.runTimers = ms => {
        for (const [fn, delay] of [...app.timers]) if (delay === ms) { app.timers.delete(fn); fn(); }
    };
    app.settle = async () => { for (let n = 0; n < 20; n++) await Promise.resolve(); };
    let source = await readFile(new URL('../src/app.js', import.meta.url), 'utf8');
    source = source.replace("import { Terminal } from './xterm.mjs';", 'const Terminal = globalThis.__TestTerminal;');
    source = source.replace("import {createMenuBar} from './menu-view.js';", 'const createMenuBar = globalThis.__TestCreateMenus;');
    source = source.replace(/from '(\.\/[^']+)'/g,
        (_, name) => `from '${new URL('../src/' + name.slice(2), import.meta.url).href}'`);
    source = source.replace(/main\(\);\s*$/, 'await main();');
    // A fresh module per test: the query makes the data URL unique.
    source += `\n//# sourceURL=app-under-test-${Math.random()}.js`;
    await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
    await app.settle();
    return app;
}
