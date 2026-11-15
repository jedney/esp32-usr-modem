import assert from 'node:assert/strict';
import test from 'node:test';
import {startDemoModem} from '../src/demo.js';

test('demo uses friendly letter lists with only ANSI colour escapes', () => {
    let output = '';
    const demo = startDemoModem({message: () => {}, write: text => { output = text; }});
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
    const root = output;
    demo.receive(new TextEncoder().encode('\x1b[B'));
    demo.receive('{"t":"size","cols":120,"rows":45}');
    assert.equal(output, root);
    demo.receive(new TextEncoder().encode('la'));
    assert.match(output, /Client Tests run on serial only/);
    assert.deepEqual(messages.filter(m => m.t === 'call'), [{t: 'call', state: 'idle'}]);
});
