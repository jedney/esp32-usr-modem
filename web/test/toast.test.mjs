import test from 'node:test';
import assert from 'node:assert/strict';
import {createToasts} from '../src/toast.js';

class Element {
    children = []; attributes = {};
    setAttribute(k, v) { this.attributes[k] = v; }
    appendChild(el) { el.parent = this; this.children.push(el); }
    remove() { this.parent.children.splice(this.parent.children.indexOf(this), 1); }
}

test('toasts stack as safe text and polite status, auto-dismiss without taking focus', () => {
    const container = new Element(), timers = new Map();
    const toasts = createToasts({container, document: {createElement: () => new Element()},
        schedule: (fn, ms) => { timers.set(fn, ms); return fn; }, cancel: fn => timers.delete(fn)});
    const dismiss = toasts.show('<img onerror=alert(1)>');
    toasts.show('Connected to Amiga Retro');
    assert.equal(container.children.length, 2);
    assert.equal(container.children[0].textContent, '<img onerror=alert(1)>');
    assert.equal(container.children[0].attributes.role, 'status');
    assert.equal(container.children[0].attributes['aria-atomic'], 'true');
    assert.equal(timers.size, 2);
    assert.ok([...timers.values()].every(ms => ms === 5000));
    dismiss(); dismiss(); assert.equal(container.children.length, 1);
    [...timers.keys()][0](); assert.equal(container.children.length, 0);
});

test('a burst has a bounded visible stack and clears old timers', () => {
    const container = new Element(), timers = new Set();
    const toasts = createToasts({container, document: {createElement: () => new Element()},
        schedule: fn => { timers.add(fn); return fn; }, cancel: fn => timers.delete(fn)});
    for (let n = 0; n < 20; n++) toasts.show(String(n));
    assert.equal(container.children.length, 5);
    assert.equal(container.children[0].textContent, '15');
    assert.equal(timers.size, 5);
});
