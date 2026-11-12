import test from 'node:test';
import assert from 'node:assert/strict';
import {createUploadFlow, xhrSender, MAX_IMAGE} from '../src/upload_flow.js';

const marker = (kind, version) => new TextEncoder().encode(`USRMODEM/1/${kind}/${version}\n`);
function firmware(version = '0.11') {
    const m = marker('FIRMWARE', version);
    const image = new Uint8Array(24 + m.length);
    image[0] = 0xe9; image[12] = 9; image.set(m, 24);
    return image;
}
function filesystem(version = '0.11') {
    const image = new Uint8Array(MAX_IMAGE).fill(0xff);
    image.set(marker('LITTLEFS', version), 100);
    return image;
}
// A page-less rig: every browser call is recorded, nothing touches a DOM.
function rig({image = firmware(), size = image.length, confirmAnswer = true, loginOk = true,
              sendResult = {status: 200, text: 'Installed; rebooting'}, sendFails = false,
              state = {}} = {}) {
    const log = {shown: [], busy: [], prompts: [], requests: [], installed: 0, logins: 0, reads: 0};
    const st = {edit: true, csrf: 'token-1', version: '0.11', upload: false, ...state};
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    const flow = createUploadFlow({
        state: st,
        show: text => log.shown.push(text),
        setBusy: busy => log.busy.push(busy),
        confirm: text => { log.prompts.push(text); return confirmAnswer; },
        read: async () => { log.reads++; await (log.holdRead ? gate : null); return image; },
        login: async () => { log.logins++; return loginOk; },
        send: async request => {
            log.requests.push(request);
            if (sendFails) throw new Error('network');
            request.onProgress(512, 1024);
            return sendResult;
        },
        installed: () => { log.installed++; },
    });
    return {flow, st, log, hold: () => { log.holdRead = true; }, release};
}
const file = (image, size = image.length) => ({size});

test('a good firmware image is checked, confirmed, sent with its hashes and reported', async () => {
    const r = rig();
    assert.equal(await r.flow(file(firmware())), 'installed');
    assert.equal(r.log.requests.length, 1);
    const q = r.log.requests[0];
    assert.equal(q.target, 'firmware'); assert.equal(q.version, '0.11');
    assert.equal(q.csrf, 'token-1'); assert.match(q.sha256, /^[0-9a-f]{64}$/);
    assert.equal(q.downgrade, false); assert.equal(q.filesystem, false);
    assert.equal(r.log.installed, 1);
    assert.ok(r.log.shown.includes('Sending 50% — wait for verification'));
    assert.equal(r.log.shown.at(-1), 'Installed; rebooting');
    assert.deepEqual(r.log.busy, [true, false]);
    assert.equal(r.st.upload, false);
});

test('a downgrade and a filesystem image each ask for their own confirmation', async () => {
    let r = rig({image: firmware('0.10')});
    assert.equal(await r.flow(file(firmware('0.10'))), 'installed');
    assert.match(r.log.prompts[0], /downgrade/);
    assert.equal(r.log.requests[0].downgrade, true);
    r = rig({image: filesystem()});
    assert.equal(await r.flow(file(filesystem())), 'installed');
    assert.match(r.log.prompts[0], /replaces sounds, web files and call history/);
    assert.equal(r.log.requests[0].filesystem, true);
    assert.equal(r.log.requests[0].target, 'littlefs');
});

test('nothing is sent when the person declines, and the state is released', async () => {
    const r = rig({confirmAnswer: false});
    assert.equal(await r.flow(file(firmware())), 'cancelled');
    assert.equal(r.log.requests.length, 0); assert.equal(r.log.logins, 0);
    assert.equal(r.log.shown.at(-1), 'Upload cancelled');
    assert.equal(r.st.upload, false); assert.deepEqual(r.log.busy, [true, false]);
});

test('read-only page, no CSRF token, no file or an upload under way are ignored', async () => {
    for (const state of [{edit: false}, {csrf: null}, {upload: true}]) {
        const r = rig({state});
        assert.equal(await r.flow(file(firmware())), 'ignored');
        assert.equal(r.log.reads, 0); assert.deepEqual(r.log.busy, []);
    }
    assert.equal(await rig().flow(null), 'ignored');
});

test('a second press during the preflight starts nothing (5.5-17)', async () => {
    const r = rig();
    r.hold();
    const first = r.flow(file(firmware()));
    assert.equal(r.st.upload, true);          // taken before the file is even read
    assert.equal(await r.flow(file(firmware())), 'ignored');
    assert.equal(r.log.reads, 1);
    r.release();
    assert.equal(await first, 'installed');
    assert.equal(r.log.requests.length, 1);
});

test('an image over the size limit, or without an identity, fails before the confirmation', async () => {
    let r = rig();
    assert.equal(await r.flow(file(firmware(), MAX_IMAGE + 1)), 'failed');
    assert.equal(r.log.shown.at(-1), 'Image too large'); assert.equal(r.log.reads, 0);
    r = rig({image: new Uint8Array(64)});
    assert.equal(await r.flow(file(null, 64)), 'failed');
    assert.match(r.log.shown.at(-1), /no USR Modem project identity/);
    assert.equal(r.log.prompts.length, 0); assert.equal(r.st.upload, false);
});

test('wrong chip, and an image whose markers disagree, are refused', async () => {
    const wrongChip = firmware(); wrongChip[12] = 0;
    let r = rig({image: wrongChip});
    assert.equal(await r.flow(file(wrongChip)), 'failed');
    assert.match(r.log.shown.at(-1), /ESP32-S3/);
    const m = (kind, v) => marker(kind, v);
    const conflict = new Uint8Array(24 + 26 + 26);
    conflict[0] = 0xe9; conflict[12] = 9;
    conflict.set(m('FIRMWARE', '0.11'), 24); conflict.set(m('FIRMWARE', '0.10'), 24 + 25);
    r = rig({image: conflict});
    assert.equal(await r.flow(file(conflict)), 'failed');
    assert.match(r.log.shown.at(-1), /Conflicting image identity/);
    assert.equal(r.log.prompts.length, 0);
});

test('an expired login stops the upload before the large body', async () => {
    const r = rig({loginOk: false});
    assert.equal(await r.flow(file(firmware())), 'failed');
    assert.equal(r.log.shown.at(-1), 'Login expired; reconnect before uploading');
    assert.equal(r.log.requests.length, 0); assert.equal(r.st.upload, false);
});

test('a socket lost during the confirmation stops the upload', async () => {
    const r = rig();
    const original = r.log.prompts;
    const flow = createUploadFlow({
        state: r.st, show: t => r.log.shown.push(t), setBusy: () => {},
        confirm: () => { r.st.csrf = null; r.st.edit = false; return true; },  // disconnect while asking
        read: async () => firmware(), login: async () => true,
        send: async () => { throw new Error('must not be called'); }, installed: () => {},
    });
    assert.equal(await flow(file(firmware())), 'failed');
    assert.equal(r.log.shown.at(-1), 'Connection lost; reconnect before uploading');
    assert.equal(original.length, 0);
});

test('a failed connection, and a refusal by the modem, are reported and release the page', async () => {
    let r = rig({sendFails: true});
    assert.equal(await r.flow(file(firmware())), 'lost');
    assert.equal(r.log.shown.at(-1), 'Upload connection lost. Check the console before retrying.');
    assert.equal(r.log.installed, 0); assert.equal(r.st.upload, false);
    r = rig({sendResult: {status: 409, text: 'Modem busy'}});
    assert.equal(await r.flow(file(firmware())), 'rejected');
    assert.equal(r.log.shown.at(-1), 'Modem busy'); assert.equal(r.log.installed, 0);
    assert.deepEqual(r.log.busy, [true, false]);
});

test('xhrSender posts raw bytes with the CSRF, hash and confirmation headers', async () => {
    const made = [];
    class FakeXhr {
        constructor() { this.headers = {}; this.upload = {}; made.push(this); }
        open(method, url) { this.method = method; this.url = url; }
        setRequestHeader(name, value) { this.headers[name] = value; }
        send(body) {
            this.body = body;
            this.upload.onprogress({loaded: 5, total: 10, lengthComputable: true});
            this.status = 200; this.responseText = 'ok'; this.onload();
        }
    }
    const progress = [];
    const send = xhrSender(() => new FakeXhr());
    const body = new Uint8Array([1, 2, 3]);
    const out = await send({target: 'littlefs', version: '0.11', sha256: 'ab'.repeat(32),
        csrf: 'tok', downgrade: true, filesystem: true, bytes: body,
        onProgress: (loaded, total) => progress.push([loaded, total])});
    const x = made[0];
    assert.deepEqual(out, {status: 200, text: 'ok'});
    assert.equal(x.method, 'POST'); assert.equal(x.url, '/api/update?target=littlefs&version=0.11');
    assert.equal(x.headers['Content-Type'], 'application/octet-stream');
    assert.equal(x.headers['X-CSRF-Token'], 'tok');
    assert.equal(x.headers['X-Image-SHA256'], 'ab'.repeat(32));
    assert.equal(x.headers['X-Confirm-Downgrade'], 'ab'.repeat(32));
    assert.equal(x.headers['X-Confirm-Filesystem'], 'ab'.repeat(32));
    assert.equal(x.timeout, 190000); assert.equal(x.body, body);
    assert.deepEqual(progress, [[5, 10]]);
});

test('xhrSender leaves out the confirmation headers when none was asked, and rejects on error or time-out', async () => {
    const made = [];
    class FakeXhr {
        constructor(mode) { this.mode = mode; this.headers = {}; this.upload = {}; made.push(this); }
        open() {}
        setRequestHeader(name, value) { this.headers[name] = value; }
        send() { this[this.mode](); }
    }
    for (const mode of ['onerror', 'ontimeout']) {
        const send = xhrSender(() => new FakeXhr(mode));
        await assert.rejects(send({target: 'firmware', version: '0.11', sha256: 'cd'.repeat(32),
            csrf: 't', downgrade: false, filesystem: false, bytes: new Uint8Array(1),
            onProgress: () => {}}), /connection lost/);
        assert.equal('X-Confirm-Downgrade' in made.at(-1).headers, false);
        assert.equal('X-Confirm-Filesystem' in made.at(-1).headers, false);
    }
});
