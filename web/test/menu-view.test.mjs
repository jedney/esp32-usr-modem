import test from 'node:test';
import assert from 'node:assert/strict';
import {createMenuBar} from '../src/menu-view.js';
import commands from '../src/ui-commands.js';

// Small DOM boundary fake. The tests exercise the actual event handlers and generated fields.
class Node {
    children = []; attributes = {}; dataset = {}; style = {}; hidden = false; disabled = false;
    value = ''; open = false; clientWidth = 1200; text = ''; className = ''; listeners = {};
    constructor(tag, doc) { this.tagName = tag; this.doc = doc; }
    set textContent(text) { this.text = String(text); this.children = []; }
    get textContent() { return this.text + this.children.map(n => n.textContent).join(''); }
    set innerHTML(text) { this.markup = text; }
    get innerHTML() { return this.markup || ''; }
    append(...nodes) { for (const node of nodes) { node.parent = this; this.children.push(node); } }
    replaceChildren(...nodes) { this.children = []; this.text = ''; this.append(...nodes); }
    setAttribute(name, value) { this.attributes[name] = value; }
    removeAttribute(name) { delete this.attributes[name]; }
    addEventListener(name, fn) { this.listeners[name] = fn; }
    showModal() { this.open = true; }
    close() { this.open = false; }
    focus() { this.doc.activeElement = this; }
    click() { if (!this.disabled) this.onclick?.(); }
    getBoundingClientRect() { return {width:this.textContent.length * 7 + 40, left:10, bottom:80}; }
    matches(selector) {
        if (selector === '[hidden]') return this.hidden;
        if (selector.startsWith('#')) return this.id === selector.slice(1);
        if (selector.startsWith('.')) return this.className.split(' ').includes(selector.slice(1));
        return this.tagName === selector;
    }
    querySelectorAll(selector) {
        const parts = selector.split(' ');
        const matches = node => {
            if (!node.matches(parts.at(-1))) return false;
            if (parts.length === 1) return true;
            let parent = node.parent;
            while (parent && !parent.matches(parts[0])) parent = parent.parent;
            return !!parent;
        };
        return this.children.flatMap(child => [ ...(matches(child) ? [child] : []), ...child.querySelectorAll(selector)]);
    }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
    closest(selector) { return this.matches(selector) ? this : this.parent?.closest(selector) || null; }
}

function setup(t) {
    const doc = {fonts:{ready:Promise.resolve()}, addEventListener() {}, createElement: tag => new Node(tag, doc)};
    doc.body = doc.createElement('body');
    doc.querySelectorAll = selector => doc.body.querySelectorAll(selector);
    doc.querySelector = selector => doc.body.querySelector(selector);
    doc.getElementById = id => doc.querySelector('#' + id);
    for (const id of ['menu', 'command-dialog', 'dialog-title','dialog-intro','dialog-icon','dialog-body','dialog-apply','dialog-cancel']) {
        const node = doc.createElement('div'); node.id = id; doc.body.append(node);
    }
    const caption = doc.createElement('span'); caption.className = 'dialog-caption'; doc.body.append(caption);
    const previous = new Map();
    for (const [name, value] of Object.entries({document:doc, innerWidth:1200, innerHeight:800,
        getComputedStyle: () => ({columnGap:'3',paddingLeft:'8',paddingRight:'8'}),
        ResizeObserver: class { observe() {} }})) {
        previous.set(name, Object.getOwnPropertyDescriptor(globalThis,name));
        Object.defineProperty(globalThis,name,{value,configurable:true,writable:true});
    }
    t.after(() => { for (const [name, value] of previous) value ? Object.defineProperty(globalThis,name,value) : delete globalThis[name]; });
    const state = {online:true,edit:true,call:{state:'idle'},busy:false};
    const sent = [], notices = [], reconnects = [];
    const menu = createMenuBar({state:() => state, run:(line, done) => {sent.push({line,done}); return true;}, focus() {}, notify:text => notices.push(text), ensureTerminal:action => reconnects.push(action)});
    const clickItem = name => doc.querySelectorAll('.menu-item').find(n => n.textContent.startsWith(name)).click();
    return {doc,state,sent,notices,menu,clickItem,reconnects};
}

test('model dialog validates input and sends exactly the reviewed command', t => {
    const {doc,sent,clickItem} = setup(t);
    const modelItem = doc.querySelectorAll('.menu-item').find(n => n.textContent.startsWith('Modem model'));
    assert.equal(modelItem.title, commands.find(c => c.id === 'settings.model').help);
    assert.equal(modelItem.querySelector('.menu-help'), null);
    clickItem('Modem model');
    assert.equal(doc.getElementById('command-dialog').open,true);
    const field = doc.getElementById('field-value');
    const apply = doc.getElementById('dialog-apply');
    apply.onclick(); assert.equal(sent.length,0);
    assert.equal(field.attributes['aria-invalid'],'true');
    field.value = '28800'; field.oninput();
    assert.equal(doc.querySelector('code').textContent,'AT*MODEL=28800');
    apply.onclick();
    assert.equal(sent[0].line,'AT*MODEL=28800\r');
    assert.equal(doc.getElementById('command-dialog').open,false);
});

test('secret values stay out of previews and are wiped on cancel and access loss', t => {
    const {doc,state,menu,clickItem} = setup(t);
    clickItem('Change device password');
    const secret = doc.getElementById('field-value'); secret.value = 'private-password'; secret.oninput();
    assert.ok(!doc.querySelector('code').textContent.includes(secret.value));
    doc.getElementById('dialog-cancel').onclick(); assert.equal(secret.value,'');
    clickItem('Change device password');
    const next = doc.getElementById('field-value'); next.value = 'private-password';
    state.edit = false; menu.update();
    assert.equal(next.value,'');
    assert.equal(doc.getElementById('command-dialog').open,false);
});

test('phonebook uses live reply data and disables actions while another command is busy', t => {
    const {doc,state,menu,sent,clickItem} = setup(t);
    clickItem('List entries');
    assert.equal(sent[0].line,'AT*PHONEBOOK?\r');
    sent[0].done('01,bbs.example.net,23,TELNET,0\r\nOK\r\n');
    assert.match(doc.querySelector('table').textContent,/bbs.example.net:23/);
    const dial = doc.querySelectorAll('button').find(n => n.textContent === 'Dial');
    state.busy = true; menu.update(); assert.equal(dial.disabled,true);
    state.busy = false; menu.update(); dial.click(); assert.equal(sent[1].line,'AT*DIAL=01\r');
});

test('profile and variant selectors rebuild fields and convert displayed units', t => {
    const {doc,sent,clickItem} = setup(t);
    clickItem('Ringing duration');
    const seconds = doc.getElementById('field-value'); assert.equal(seconds.step,0.001);
    seconds.value = '2.5'; seconds.oninput(); doc.getElementById('dialog-apply').onclick();
    assert.equal(sent[0].line,'AT*RINGDELAY=2500\r');
    clickItem('Modem profile settings');
    const operation = doc.getElementById('command-operation'); operation.value = 'S7'; operation.onchange();
    doc.getElementById('field-value').value = '45'; doc.getElementById('dialog-apply').onclick();
    assert.equal(sent[1].line,'AT*SETTING=S7=45\r');
});

test('hardware stop stays reachable while new commands are disabled', async t => {
    const {doc,state,menu} = setup(t);
    await Promise.resolve(); // Apply the initial one-row overflow layout.
    state.busy = true; state.hardware = {active:true};
    doc.getElementById('stop-test').hidden = false;
    menu.update();
    assert.equal(doc.getElementById('stop-test').disabled,false);
    assert.equal(doc.querySelectorAll('.menu-trigger').find(n => n.textContent.startsWith('Phonebook')).disabled,true);
    assert.equal(doc.querySelectorAll('.menu-trigger').find(n => n.textContent.startsWith('Hardware Tests')).disabled,false);
});

test('terminal-only actions live in Debug, Updates and Hardware Tests', t => {
    const {doc,state,menu} = setup(t);
    assert.equal(doc.getElementById('recent-console-history').parent.id, 'popup-debug');
    assert.equal(doc.getElementById('upload').parent.id, 'popup-updates-reset');
    assert.equal(doc.getElementById('line-controls').parent.id, 'popup-hardware-tests');
    assert.equal(doc.getElementById('stop-test').parent.id, 'popup-hardware-tests');
    state.hardware = {active:true,mode:'lines'}; state.busy = true; menu.update();
    assert.equal(doc.getElementById('line-controls').hidden,false);
    assert.equal(doc.getElementById('dcd').disabled,false);
    assert.equal(doc.getElementById('recent-console-history').disabled,true);
    state.hardware = {active:false}; menu.update();
    assert.equal(doc.getElementById('line-controls').hidden,true);
    assert.equal(doc.getElementById('stop-test').hidden,true);
});

test('disconnected menu selection requests access before opening the chosen menu', t => {
    const {doc,state,menu,reconnects} = setup(t);
    state.edit = false; state.online = false; state.busy = true; menu.update();
    const trigger = doc.querySelectorAll('.menu-trigger').find(n => n.textContent.startsWith('Settings'));
    assert.equal(trigger.disabled,false);
    trigger.click();
    assert.equal(reconnects.length,1);
    assert.equal(doc.getElementById('popup-settings').hidden,true);
    state.edit = true; state.online = true; state.busy = false; menu.update();
    reconnects[0]();
    assert.equal(doc.getElementById('popup-settings').hidden,false);
});
