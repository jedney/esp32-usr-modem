import test from 'node:test';
import assert from 'node:assert/strict';
import {createIdleGuard} from '../src/idle.js';

function rig() {
    let time = 0;
    const prompts = [], hangups = [];
    const guard = createIdleGuard({now: () => time, prompt: n => prompts.push(n),
        hangup: () => hangups.push(time)});
    return {guard, prompts, hangups, at: n => { time = n; }};
}

test('only a browser-owned connected call arms the idle guard', () => {
    const r = rig();
    r.at(900000); r.guard.poll();
    assert.deepEqual(r.hangups, []);
    r.guard.connected(true);
    r.at(1139999); r.guard.poll(); assert.deepEqual(r.prompts, []);
    r.at(1140000); r.guard.poll(); assert.deepEqual(r.prompts, [60]);
    r.guard.connected(true); // repeated call snapshots must not reset the deadline
    r.at(1141000); r.guard.poll(); assert.equal(r.prompts.at(-1), 59);
    r.at(1200000); r.guard.poll();
    assert.deepEqual(r.hangups, [1200000]);
    r.at(1800000); r.guard.poll(); assert.equal(r.hangups.length, 1);
});

test('typing, clicking, movement or the here button resets the same activity timestamp', () => {
    const r = rig(); r.guard.connected(true);
    r.at(239000); r.guard.activity();
    r.at(240000); r.guard.poll(); assert.deepEqual(r.prompts, []);
    r.at(479000); r.guard.poll(); assert.equal(r.prompts.at(-1), 60);
    r.at(480000); r.guard.activity(); assert.equal(r.prompts.at(-1), null);
    r.at(719999); r.guard.poll(); assert.deepEqual(r.hangups, []);
    r.at(720000); r.guard.poll(); assert.equal(r.prompts.at(-1), 60);
});

test('disconnect disarms, and a later call gets a fresh deadline', () => {
    const r = rig(); r.guard.connected(true);
    r.at(240000); r.guard.poll(); r.guard.connected(false);
    assert.equal(r.prompts.at(-1), null);
    r.at(900000); r.guard.poll(); assert.deepEqual(r.hangups, []);
    r.guard.connected(true); r.at(1140000); r.guard.poll();
    assert.equal(r.prompts.at(-1), 60);
});

test('throttled tabs use elapsed time, including skipping the entire countdown', () => {
    const r = rig(); r.guard.connected(true);
    r.at(275100); r.guard.poll(); assert.equal(r.prompts.at(-1), 25);
    r.at(600000); r.guard.poll(); assert.equal(r.hangups.length, 1);
    const skipped = rig(); skipped.guard.connected(true);
    skipped.at(600000); skipped.guard.poll(); assert.equal(skipped.hangups.length, 1);
});
