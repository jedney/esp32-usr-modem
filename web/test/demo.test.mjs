import assert from 'node:assert/strict';
import test from 'node:test';
import {startDemoModem} from '../src/demo.js';

test('demo uses friendly letter lists with only ANSI colour escapes', () => {
    let output = '';
    const demo = startDemoModem({message: () => {}, write: text => { output = text; }});
    demo.receive('{"t":"open"}');
    demo.receive(new TextEncoder().encode('AT*CONFIG\r'));
    assert.match(output, /A\. Status/);
    assert.match(output, /L\. Client Tests/);
    assert.equal(output.replace(/\x1b\[[\d;]*m/g, '').includes('\x1b'), false);
    demo.receive(new TextEncoder().encode('c\r\n'));
    assert.match(output, /Rings before answering/);
    demo.receive(new TextEncoder().encode('c'));
    assert.match(output, /A\. US Robotics Sportster 14.4k/);
});

test('demo ignores arrows and sizes, and Client Tests stay serial only', () => {
    const messages = [];
    let output = '';
    const demo = startDemoModem({message: m => messages.push(m), write: text => { output = text; }});
    demo.receive('{"t":"open"}');
    demo.receive(new TextEncoder().encode('AT*CONFIG\r'));
    const root = output;
    demo.receive(new TextEncoder().encode('\x1b[B'));
    demo.receive('{"t":"size","cols":120,"rows":45}');
    assert.equal(output, root);
    demo.receive(new TextEncoder().encode('la'));
    assert.match(output, /Client Tests run on serial only/);
    assert.deepEqual(messages.filter(m => m.t === 'call'), [{t: 'call', state: 'idle'}]);
});

test('demo starts read-only, opens explicitly, dials synthetic data and hangs up without closing', () => {
    const messages = [], output = [];
    const demo = startDemoModem({message: m => messages.push(m), write: text => output.push(text)});
    const send = text => demo.receive(new TextEncoder().encode(text));
    assert.equal(messages.at(-1).state, 'read-only');
    send('e\r'); assert.deepEqual(output, []);
    demo.receive('{"t":"open"}');
    assert.equal(messages.at(-1).state, 'terminal');
    send('e\r'); assert.match(output.at(-1), /ERROR/);
    demo.receive('{"t":"size","cols":300,"rows":100}');
    send('AT*SIZE?\r'); assert.match(output.at(-1), /300,100/);
    send('ATDT1\r');
    assert.equal(messages.at(-1).state, 'connected');
    assert.equal(messages.at(-1).browser, true);
    demo.receive('{"t":"hangup"}');
    assert.equal(messages.at(-1).state, 'idle');
    send('AT\r'); assert.match(output.at(-1), /OK/);
    send('ATDT1\r'); send('+'); send('+'); send('+'); send('ATH\r');
    assert.equal(messages.at(-1).state, 'idle');
    demo.close(); assert.equal(messages.at(-1).state, 'read-only');
    demo.receive('{"t":"open"}'); send('AT\r'); assert.match(output.at(-1), /OK/);
});
