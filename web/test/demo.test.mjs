import assert from 'node:assert/strict';
import test from 'node:test';
import {startDemoModem} from '../src/demo.js';

test('demo uses the full catalogue and D cannot place even a simulated call', () => {
    let output = '';
    const messages = [];
    const demo = startDemoModem({message: msg => messages.push(msg), write: text => { output = text; }});
    assert.match(output, /Client Tests/);
    assert.match(output, /Export Configuration/);
    demo.receive(new TextEncoder().encode('d\r\n'));
    assert.match(output, /Display patterns/);
    assert.equal(messages.filter(m => m.t === 'call').length, 1);
    assert.equal(messages.find(m => m.t === 'call').state, 'idle');
    demo.receive('{"t":"size","cols":120,"rows":45}');
    assert.match(output, /Display patterns/);
});
test('demo call requires Phonebook selection and confirmation', () => {
    const messages = [];
    let output = '';
    const demo = startDemoModem({message: msg => messages.push(msg), write: text => { output = text; }});
    const keys = text => demo.receive(new TextEncoder().encode(text));
    keys('2\r4\r');
    assert.match(output, /No network connection is made/);
    assert.equal(messages.filter(m => m.t === 'call').length, 1);
    keys('y\r');
    assert.equal(messages.filter(m => m.t === 'call').at(-1).host, 'demo.invalid');
    keys('h');
    assert.equal(messages.filter(m => m.t === 'call').at(-1).state, 'idle');
});
