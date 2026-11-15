import test from 'node:test';
import assert from 'node:assert/strict';
import {createModemLink, BUSY_DELAY} from '../src/link.js';
class Socket {
    readyState = 0;
    sent = [];
    bufferedAmount = 0;
    close() { this.readyState = 2; }
    send(data) { this.sent.push(data); }
    open() { this.readyState = 1; this.onopen(); }
}
function rig(session) {
    const sockets = [], timers = [], statuses = [], received = [];
    let time = 0;
    const link = createModemLink({
        session: session || (async () => 'ticket'),
        socket: token => {const ws = new Socket(); ws.token = token; sockets.push(ws); return ws;},
        receive: data => received.push(data), status: s => statuses.push(s), disconnect: () => {},
        now: () => time,
        schedule: (fn,ms) => {const timer = {fn,ms}; timers.push(timer); return timer;},
        cancel: timer => timers.splice(timers.indexOf(timer),1),
    });
    return {link,sockets,timers,statuses,received,setTime: value => time = value,
        retry: async () => {timers.shift().fn(); await Promise.resolve(); await Promise.resolve();}};
}
test('heartbeat proves liveness without sending terminal input', async () => {
    const r = rig(); await r.link.start(); const ws = r.sockets[0];
    assert.equal(r.link.send('key'),false); ws.open();
    r.setTime(5000); r.link.poll();
    assert.deepEqual(ws.sent,['{"t":"ping"}']);
    ws.onmessage({data:'{"t":"pong"}'});
    r.setTime(19000); r.link.poll();
    assert.equal(r.timers.length,0); assert.equal(r.link.send(new Uint8Array([65])),false);
    ws.onmessage({data:'{"t":"access","edit":true,"state":"terminal"}'});
    assert.equal(r.link.send(new Uint8Array([65])),true);
});

test('opening is explicit, access must grant terminal mode, and closing reconnects read-only', async () => {
    const r = rig(); await r.link.start(); const ws = r.sockets[0]; ws.open();
    assert.deepEqual(ws.sent, []);
    assert.equal(r.link.openTerminal(), true);
    assert.deepEqual(ws.sent, ['{"t":"open"}']);
    ws.onmessage({data:'{"t":"access","edit":true,"state":"read-only"}'});
    assert.equal(r.link.send(new Uint8Array([65])), false);
    ws.onmessage({data:'{"t":"access","edit":true,"state":"terminal"}'});
    assert.equal(r.link.send('{"t":"size","cols":300,"rows":100}'), true);
    await r.link.closeTerminal();
    assert.equal(ws.readyState, 2);
    r.sockets[1].open();
    assert.equal(r.link.send(new Uint8Array([65])), false);
    assert.deepEqual(r.sockets[1].sent, []);
});

test('paste is chunked, paced by bufferedAmount, ordered with keys and bounded', async () => {
    const r = rig(); await r.link.start(); const ws = r.sockets[0]; ws.open();
    ws.onmessage({data:'{"t":"access","edit":true,"state":"terminal"}'});
    const paste = Uint8Array.from({length: 1600}, (_, i) => i % 256);
    ws.bufferedAmount = 2048;
    assert.equal(r.link.send(paste), true);
    assert.deepEqual(ws.sent, []);
    assert.equal(r.link.send(new Uint8Array(1024 * 1024)), false);
    ws.bufferedAmount = 0;
    r.timers.shift().fn();
    assert.equal(ws.sent[0].length, 512);
    r.link.send(new Uint8Array([13]));
    while (r.timers.length) r.timers.shift().fn();
    assert.ok(ws.sent.every(frame => frame.length <= 512));
    assert.deepEqual(Uint8Array.from(ws.sent.flatMap(b => [...b])), Uint8Array.from([...paste, 13]));
});

test('revocation, upload, hangup and socket loss discard queued paste', async () => {
    for (const action of ['access', 'upload', 'hangup', 'disconnect']) {
        const r = rig(); await r.link.start(); const ws = r.sockets[0]; ws.open();
        ws.onmessage({data:'{"t":"access","edit":true,"state":"terminal"}'});
        ws.bufferedAmount = 2048;
        r.link.send(new Uint8Array(2000));
        if (action === 'access') ws.onmessage({data:'{"t":"access","edit":false,"state":"read-only"}'});
        if (action === 'upload') r.link.pause(true);
        if (action === 'hangup') assert.equal(r.link.hangup(), true);
        if (action === 'disconnect') ws.onclose();
        ws.bufferedAmount = 0;
        if (action !== 'disconnect') while (r.timers.length) r.timers.shift().fn();
        assert.ok(ws.sent.every(data => typeof data === 'string'));
        if (action === 'hangup') assert.deepEqual(ws.sent, ['{"t":"hangup"}']);
    }
});
test('a busy modem (update in progress) is asked again only every 30 seconds', async () => {
    const busy = Object.assign(new Error('Login unavailable'), {busy: true});
    let calls = 0;
    const r = rig(async () => { calls++; if (calls < 3) throw busy; return 'ticket'; });
    await r.link.start();
    assert.equal(r.timers[0].ms, BUSY_DELAY);
    await r.retry();
    assert.equal(r.timers[0].ms, BUSY_DELAY);
    await r.retry();
    assert.equal(r.sockets.length, 1);
    // Any other failure keeps the normal back-off: 1 s, 2 s, 5 s, then 10 s.
    const plain = rig(async () => { throw new Error('Login unavailable'); });
    await plain.link.start();
    assert.equal(plain.timers[0].ms, 1000);
    await plain.retry();
    assert.equal(plain.timers[0].ms, 2000);
});
test('half-open reset reconnects without a socket close event', async () => {
    const r = rig(); await r.link.start(); const old = r.sockets[0]; old.open();
    r.setTime(15000); r.link.poll();
    assert.equal(old.readyState,2); assert.equal(r.timers.length,1);
    assert.equal(r.link.send('stale key'),false);
    await r.retry(); r.sockets[1].open(); old.onmessage({data:'old status'});
    assert.deepEqual(r.received,[]); assert.equal(r.timers.length,0);
});
test('upload pause prevents false expiry; completion forces a fresh socket', async () => {
    const r = rig(); await r.link.start(); r.sockets[0].open();
    r.setTime(120000); r.link.poll(true);
    assert.equal(r.timers.length,0); assert.deepEqual(r.sockets[0].sent,[]);
    r.link.reconnect(); assert.equal(r.timers.length,1);
    await r.retry(); assert.equal(r.sockets.length,2);
});
test('stale ticket fetch cannot attach after reconnect', async () => {
    let resolveFirst, calls = 0;
    const r = rig(() => ++calls === 1 ? new Promise(resolve => {resolveFirst=resolve;}) : Promise.resolve('fresh'));
    const initial = r.link.start(); r.link.reconnect(); await r.retry();
    resolveFirst('stale'); await initial;
    assert.equal(r.sockets.length,1); assert.equal(r.sockets[0].token,'fresh');
});
test('failed login backs off and stopping cancels retries', async () => {
    const r = rig(async () => {throw new Error('offline');});
    await r.link.start(); assert.equal(r.timers[0].ms,1000);
    await r.retry(); assert.equal(r.timers[0].ms,2000);
    r.link.stop(); assert.equal(r.timers.length,0);
});
