import test from 'node:test';
import assert from 'node:assert/strict';
import commands from '../src/ui-commands.js';

test('typed command catalogue loads in JavaScript with portable patterns', () => {
  assert.equal(commands.length, 91);
  function visit(node) {
    if (node.pattern) assert.doesNotThrow(() => new RegExp(`^(?:${node.pattern})$`));
    for (const child of [...(node.fields || []), ...(node.variants || [])]) visit(child);
  }
  commands.forEach(visit);
});

test('browser validation patterns reject reserved numbers and invalid network names', () => {
  const byId = Object.fromEntries(commands.map(command => [command.id, command]));
  const matches = (node, value) => new RegExp(`^(?:${node.pattern})$`, 'u').test(value);
  const number = byId['phonebook.add-edit-entry'].fields.find(field => field.id === 'num');
  for (const value of ['123', '666', '667', '5517545', '5517546', '1-2', '1'.repeat(12)]) {
    assert.equal(matches(number, value), false, value);
  }
  assert.equal(matches(number, '0001'), true);
  for (const value of ['a..b', '.host', '-host', 'host.', 'host\n']) {
    assert.equal(matches(byId['wifi-clock.ntp-server'], value), false, value);
  }
  assert.equal(matches(byId['wifi-clock.ntp-server'], 'pool.ntp.org'), true);
  assert.equal(matches(byId['wifi-clock.network-name'], 'USR-modem'), true);
  assert.equal(matches(byId['wifi-clock.network-name'], 'modem-'), false);
});
