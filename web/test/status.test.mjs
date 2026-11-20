// Unit tests for the web status bar helpers. Run with: node --test web/test
import assert from 'node:assert/strict';
import test from 'node:test';

import * as S from '../src/status.js';

test('LED mask decodes in front-panel order', () => {
    assert.deepEqual(S.LEDS.map((l) => l.id), ['AA', 'CD', 'RD', 'SD', 'TR', 'CS', 'ARQ']);
    const all = S.decodeLeds(0b1111111);
    assert.ok(Object.values(all).every(Boolean));
    const some = S.decodeLeds(0b1010010); // CD, TR, ARQ
    assert.deepEqual(some, { AA: false, CD: true, RD: false, SD: false, TR: true, CS: false, ARQ: true });
    assert.ok(Object.values(S.decodeLeds(undefined)).every((v) => v === false));
});

test('durations', () => {
    assert.equal(S.formatDuration(0), '00:00:00');
    assert.equal(S.formatDuration(754.9), '00:12:34');
    assert.equal(S.formatDuration(86399), '23:59:59');
    assert.equal(S.formatDuration(90061), '1d 01:01:01');
    assert.equal(S.formatDuration(-1), '--:--:--');
    assert.equal(S.formatDuration(NaN), '--:--:--');
});

test('modem clock uses the modem time zone, not the browser', () => {
    // 2026-10-23 09:27:05 UTC = 22:27:05 NZDT (UTC+13)
    const epoch = Date.UTC(2026, 9, 23, 9, 27, 5) / 1000;
    assert.equal(S.formatClock(epoch, 780), 'Fri 23 Oct 22:27:05');
    assert.equal(S.formatClock(epoch, 0), 'Fri 23 Oct 09:27:05');
    assert.equal(S.formatClock(0, 780), 'clock not set');
});

test('call status labels', () => {
    assert.deepEqual(S.describeCall({ state: 'idle' }), { label: 'Idle', cls: 'idle' });
    assert.deepEqual(S.describeCall({ state: 'dialling' }), { label: 'Dialling', cls: 'busy' });
    assert.deepEqual(S.describeCall({ state: 'connected', mode: 'telnet', speed: 33600 }),
        { label: 'Connected · Telnet · 33600', cls: 'up' });
    assert.equal(S.describeCall({ state: 'connected', dir: 'in', mode: 'raw' }).label,
        'Connected (incoming) · Raw');
    assert.equal(S.describeCall({ state: 'connected', mode: 'ssh', speed: 28800 }).label,
        'Connected · SSH · 28800');
    assert.equal(S.describeCall({ state: 'ppp' }).label, 'Internet (PPP)');
    assert.equal(S.describeCall({ state: 'bogus' }).label, 'Unknown');
    assert.equal(S.describeCall(null).label, 'Unknown');
});

test('remote host and IP', () => {
    const call = { state: 'connected', host: 'www.amigaretro.com', ip: '170.64.142.64', port: 23 };
    assert.equal(S.describeRemote(call), 'www.amigaretro.com (170.64.142.64:23)');
    assert.equal(S.describeRemote({ ...call, host: '170.64.142.64' }), '170.64.142.64:23');
    assert.equal(S.describeRemote({ ...call, ip: '' }), 'www.amigaretro.com:23');
    assert.equal(S.describeRemote({ ...call, host: '' }), '170.64.142.64:23');
    assert.equal(S.describeRemote({ ...call, state: 'idle' }), '—');
    assert.equal(S.describeRemote({ state: 'connected' }), '—');
});

test('call uptime only while connected', () => {
    assert.equal(S.callUptime({ state: 'connected', since: 1000 }, 1754), 754);
    assert.equal(S.callUptime({ state: 'ppp', since: 1000 }, 999), 0);
    assert.equal(S.callUptime({ state: 'dialling', since: 1000 }, 1754), null);
    assert.equal(S.callUptime({ state: 'connected' }, 1754), null);
});

test('Wi-Fi signal bars', () => {
    assert.equal(S.rssiBars(-50), 4);
    assert.equal(S.rssiBars(-60), 3);
    assert.equal(S.rssiBars(-70), 2);
    assert.equal(S.rssiBars(-80), 1);
    assert.equal(S.rssiBars(-95), 0);
    assert.equal(S.rssiBars(undefined), 0);
});

test('reconnect back-off', () => {
    assert.deepEqual([0, 1, 2, 3, 9].map(S.reconnectDelay), [1000, 2000, 5000, 10000, 10000]);
});

test('font size fits 80 x 25 in the window', () => {
    // 0.6 em wide, 1.3 em tall cells
    assert.equal(S.fitFontSize(1280, 650, 80, 25, 0.6, 1.3), 20); // height-limited
    assert.equal(S.fitFontSize(640, 900, 80, 25, 0.6, 1.3), 13); // width-limited
    assert.equal(S.fitFontSize(100, 100, 80, 25, 0.6, 1.3), 8); // clamped
    assert.equal(S.fitFontSize(0, 0, 80, 25, 0.6, 1.3), 16); // not laid out yet
});

test('messages from the modem', () => {
    assert.deepEqual(S.parseMessage('{"t":"led","m":5}'), { t: 'led', m: 5 });
    assert.equal(S.parseMessage('not json'), null);
    assert.equal(S.parseMessage('{"m":5}'), null);
    assert.equal(S.parseMessage('5'), null);
});

test('access state and browser call identity guard input and idle timeout', () => {
    assert.equal(S.terminalAccess({edit: true, state: 'terminal'}), true);
    assert.equal(S.terminalAccess({edit: true, state: 'read-only'}), false);
    assert.equal(S.terminalAccess({edit: false, state: 'terminal'}), false);
    assert.equal(S.browserCallConnected({state: 'connected', browser: true}, true), true);
    for (const call of [{state: 'connected'}, {state: 'connected', browser: false},
        {state: 'dialling', browser: true}, {state: 'idle'}]) {
        assert.equal(S.browserCallConnected(call, true), false);
    }
    assert.equal(S.browserCallConnected({state: 'connected', browser: true}, false), false);
});

test('event toasts cover access, call identity and reason, loss and restore without duplicates', () => {
    const messages = [], events = S.createStatusEvents(text => messages.push(text));
    events.link('connecting'); events.link('offline'); events.link('online');
    assert.deepEqual(messages, []);
    events.access({edit: false, state: 'read-only'}, true);
    assert.match(messages.at(-1), /Another browser has the terminal/);
    events.access({edit: true, state: 'terminal'});
    assert.equal(messages.at(-1), 'Terminal opened');
    const call = {state: 'connected', host: 'Amiga Retro', number: '1', browser: true};
    events.call(call); events.call({...call, speed: 14400});
    assert.equal(messages.filter(s => s.startsWith('Connected')).length, 1);
    assert.equal(messages.at(-1), 'Connected to Amiga Retro');
    events.away(); events.call({state: 'idle'});
    assert.equal(messages.at(-1), 'Disconnected: Hung up because you were away');
    events.call({state: 'connected', number: '42'}); events.call({state: 'idle', reason: 'No carrier'});
    assert.equal(messages.at(-1), 'Disconnected: No carrier');
    events.link('offline'); events.link('offline'); events.link('connecting'); events.link('online');
    assert.deepEqual(messages.slice(-2), ['Link lost', 'Link restored']);
    events.access({edit: false, state: 'read-only'});
    assert.equal(messages.at(-1), 'Terminal closed');
});

test('command refusal spans frames but BBS output and read-only bytes never create it', () => {
    const messages = [], events = S.createStatusEvents(text => messages.push(text));
    const bytes = text => new TextEncoder().encode(text);
    events.output(bytes('ERROR\r\n'));
    assert.equal(messages.length, 0);
    events.access({edit: true, state: 'terminal'});
    events.output(bytes('\r\nER')); events.output(bytes('ROR\r\n'));
    assert.equal(messages.at(-1), 'Command refused');
    const refusals = messages.length;
    events.output(bytes('4\r\n'));
    assert.equal(messages.length, refusals + 1);
    events.call({state: 'connected'});
    const before = messages.length;
    events.output(bytes('ERROR\r\n')); assert.equal(messages.length, before);
    events.disconnected(); events.call({state: 'idle'});
    assert.equal(messages.filter(s => s.startsWith('Disconnected')).length, 0);
});
