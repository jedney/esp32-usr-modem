import test from 'node:test';
import assert from 'node:assert/strict';
import commands from '../src/ui-commands.js';
import {menuSections, menuDisabledReason, visibleMenuCount, commandForm, buildCommand, parsePhonebook, parseNetworks} from '../src/menubar.js';

test('bar follows the generated configuration layout and omits serial-only client tests', () => {
    const sections = menuSections();
    assert.deepEqual(sections.map(s => s.id), commands
        .filter(c => c.screen === 'Main' && c.id !== 'client-tests').map(c => c.id));
    for (const section of sections) {
        assert.ok(section.friendly && section.help && section.icon);
        for (const item of section.items) {
            assert.ok(item.friendly && item.help);
            assert.ok(commands.includes(item));
            assert.ok(item.template || item.fields?.every(field => field.template || field.readOnly));
        }
    }
    assert.ok(sections.find(s => s.id === 'settings').items.some(i => i.id === 'settings.model'));
    assert.ok(!sections.flatMap(s => s.items).some(i => i.screen === 'Client'));
});

test('bar explains offline, read-only, upload and busy states', () => {
    assert.match(menuDisabledReason({}), /connect/i);
    assert.match(menuDisabledReason({online: true}), /open the terminal/i);
    assert.match(menuDisabledReason({online: true, edit: true, upload: true}), /upload/i);
    assert.match(menuDisabledReason({online: true, edit: true, busy: true}), /finish/i);
    assert.match(menuDisabledReason({online: true, edit: true, call: {state: 'connected'}}), /hang up/i);
    assert.equal(menuDisabledReason({online: true, edit: true, call: {state: 'idle'}}), '');
});

test('menus fit one row and reserve space for More only when needed', () => {
    assert.equal(visibleMenuCount(300, [100, 100, 100], 70), 3);
    assert.equal(visibleMenuCount(299, [100, 100, 100], 70), 2);
    assert.equal(visibleMenuCount(190, [100, 100, 100], 70), 1);
    assert.equal(visibleMenuCount(90, [100, 100, 100], 70), 0);
    assert.equal(visibleMenuCount(300, [], 70), 0);
});

const byId = id => commands.find(c => c.id === id);
const example = field => field.values?.[0].value ?? (field.type === 'int'
    ? String((field.min ?? 0) * (field.displayScale ?? 1))
    : field.type === 'ip' ? '192.168.1.1' : field.type === 'secret' ? 'abcdefgh' : 'example');

test('every browser menu action, variant and editable register builds one valid line', () => {
    for (const item of menuSections().flatMap(s => s.template ? [s] : s.items)) {
        const variants = item.variants || (item.template ? [null] : item.fields.filter(f => !f.readOnly));
        for (const variant of variants) {
            const form = commandForm(item, variant?.id);
            const values = Object.fromEntries(form.fields.map(f => [f.id, example(f)]));
            if (values.num) values.num = '1';
            if (values.number) values.number = '1';
            if (values.local) { values.local = '192.168.1.1'; values.peer = '192.168.1.2'; }
            if (values.ip) { values.mask = '255.255.255.0'; values.gateway = '192.168.1.254'; }
            if (item.id === 'phonebook.delete-entry') values.value = '1';
            if (item.id === 'hardware-tests.dtmf' || item.id === 'hardware-tests.pulse') values.value = '1';
            if (item.id === 'settings.stored-number') values.number = '1';
            assert.doesNotThrow(() => {
                const line = buildCommand(form, values);
                assert.match(line, /^AT\*[^\r\n{}]+\r$/);
            }, `${item.id} / ${variant?.id}`);
        }
    }
});

test('forms validate ranges, UTF-8 byte lengths, enums, secrets and line injection', () => {
    const build = (id, value) => buildCommand(commandForm(byId(id)), {value});
    assert.equal(build('sounds.ring-delay', '1.25'), 'AT*RINGDELAY=1250\r');
    for (const value of ['-1', '61', '1.0001', 'NaN', ''])
        assert.throws(() => build('sounds.ring-delay', value));
    assert.throws(() => build('settings.model', '999'));
    assert.throws(() => build('updates-reset.change-device-password', 'short'));
    assert.equal(build('updates-reset.change-device-password', 'pass{}, '), 'AT*PASS=pass{}, \r');
    assert.throws(() => build('wifi-clock.network-name', 'AT\rAT*FACTORY'));
    assert.throws(() => build('wifi-clock.ntp-server', 'bad..name'));
    const join = commandForm(byId('wifi-clock.join'));
    assert.equal(buildCommand(join, {ssid: 'Café "home"'}), 'AT*WIFIJOIN="Café "home""\r');
    assert.throws(() => buildCommand(join, {ssid: 'é'.repeat(17)}));
    assert.throws(() => buildCommand(join, {ssid: 'home\x1b'}));
    const book = commandForm(byId('phonebook.add-edit-entry'));
    assert.equal(buildCommand(book, {num:'1', host:'bbs.example.org', port:'23', proto:'TELNET', speed:''}),
        'AT*PHONEBOOK=1,bbs.example.org,23,TELNET\r');
    assert.throws(() => buildCommand(book, {num:'666',host:'host',port:'23',proto:'TELNET'}));
    assert.throws(() => build('phonebook.dial', 'host\rAT*RESTART'));
});

test('network composite forms reject invalid subnets before dispatch', () => {
    const form = commandForm(byId('wifi-clock.ip-configuration'), 'static');
    assert.throws(() => buildCommand(form, {ip:'192.168.1.2',mask:'255.0.255.0',gateway:'192.168.1.1'}));
    assert.throws(() => buildCommand(form, {ip:'192.168.1.2',mask:'255.255.255.0',gateway:'192.168.2.1'}));
    const internet = commandForm(byId('wifi-clock.internet-settings'), 'ip');
    assert.throws(() => buildCommand(internet, {local:'8.8.8.1',peer:'8.8.8.2'}));
});

test('phonebook replies become safe table data without trusting echoed commands or diagnostics', () => {
    assert.deepEqual(parsePhonebook('AT*PHONEBOOK?\r\n01,bbs.example.org,23,TELNET,0\r\nOK\r\n'),
        [{num:'01',host:'bbs.example.org',port:'23',proto:'TELNET',speed:'0'}]);
    assert.deepEqual(parsePhonebook('ERROR\r\n<script>\r\n'), []);
});

test('scan choices retain network names and safe numeric selection values', () => {
    assert.deepEqual(parseNetworks(' 1  Café network                      -48 dBm  ch  6  WPA2\r\nPress a key\r\n'),
        [{value:'1',label:'Café network · -48 dBm',description:'WPA2'}]);
    assert.deepEqual(parseNetworks('21  Outside catalogue                  -48 dBm  ch  6  WPA2\r\n'), []);
});
