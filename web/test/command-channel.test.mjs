import test from 'node:test';
import assert from 'node:assert/strict';
import {createCommandChannel} from '../src/command-channel.js';

test('menu waits for firmware to consume all accepted input and become idle', () => {
    const sent = [], replies = [];
    const channel = createCommandChannel({send: bytes => {sent.push(bytes); return true;}, changed() {}, show() {}});
    assert.equal(channel.busy(), true);
    channel.ready({idle:true, rx:0}); assert.equal(channel.busy(), false);
    channel.request('AT*PHONEBOOK?\r', reply => replies.push(reply));
    assert.equal(channel.busy(), true);
    channel.ready({idle:true, rx:0}); assert.equal(channel.busy(), true);
    channel.output(new TextEncoder().encode('1,bbs.example.org,23,TELNET,0\r\n'));
    channel.ready({idle:false, rx:sent[0].length}); assert.equal(replies.length, 0);
    channel.ready({idle:true, rx:sent[0].length});
    assert.equal(channel.busy(), false);
    assert.equal(replies.length, 1);
    assert.match(replies[0], /bbs.example.org/);
});

test('failed sends, oversized capture and disconnect release pending requests safely', () => {
    let accept = false;
    const notices = [], replies = [];
    const channel = createCommandChannel({send: () => accept, changed() {}, show: text => notices.push(text)});
    channel.ready({idle:true, rx:0});
    assert.equal(channel.request('AT\r'), false);
    assert.equal(channel.busy(), false);
    accept = true;
    channel.request('AT*PHONEBOOK?\r', reply => replies.push(reply));
    channel.output(new Uint8Array(40000));
    channel.ready({idle:true, rx:new TextEncoder().encode('AT*PHONEBOOK?\r').length});
    assert.equal(replies[0], null);
    assert.ok(notices.some(t => /too large/i.test(t)));
    channel.request('AT\r', reply => replies.push(reply));
    channel.reset();
    assert.equal(replies.at(-1), null);
    assert.equal(channel.busy(), true);
});

test('quiet command failures still notify, and textual failures are not notified twice', () => {
    const notices = [], replies = [];
    const channel = createCommandChannel({send: () => true, changed() {}, show:text => notices.push(text)});
    channel.ready({idle:true, rx:0, errors:10});
    assert.equal(notices.length,0);
    channel.request('AT*PHONEBOOK?\r', reply => replies.push(reply));
    channel.ready({idle:true, rx:14, errors:11});
    assert.deepEqual(notices,['Command refused']);
    assert.deepEqual(replies,[null]);
    channel.request('AT\r');
    channel.output(new TextEncoder().encode('\r\nERROR\r\n'));
    channel.ready({idle:true, rx:17, errors:12});
    assert.equal(notices.length,1);
});
