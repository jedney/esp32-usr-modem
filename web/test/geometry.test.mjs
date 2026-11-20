import assert from 'node:assert/strict';
import test from 'node:test';
import {terminalGeometry, sizeMessage} from '../src/geometry.js';

test('adaptive geometry retains compact, intermediate and wide screens within bounds', () => {
    assert.deepEqual(terminalGeometry(400, 300, 10, 20), {cols: 80, rows: 25});
    assert.deepEqual(terminalGeometry(1000, 500, 10, 20), {cols: 100, rows: 25});
    assert.deepEqual(terminalGeometry(1200, 900, 10, 20), {cols: 120, rows: 45});
    assert.deepEqual(terminalGeometry(3000, 2000, 10, 20), {cols: 300, rows: 100});
    assert.deepEqual(terminalGeometry(1e9, 1e9, 10, 20), {cols: 65535, rows: 65535});
    assert.deepEqual(terminalGeometry(0, 500, 10, 20), {cols: 80, rows: 25});
    assert.deepEqual(terminalGeometry(1000, 500, 0, 20), {cols: 80, rows: 25});
});
test('geometry messages have canonical ordering and enforce device bounds', () => {
    assert.equal(sizeMessage(120, 45), '{"t":"size","cols":120,"rows":45}');
    assert.equal(sizeMessage(300, 100), '{"t":"size","cols":300,"rows":100}');
    assert.equal(sizeMessage(65535, 65535), '{"t":"size","cols":65535,"rows":65535}');
    for (const [cols, rows] of [[79, 25], [65536, 25], [80, 24], [80, 65536], [80.5, 25], [NaN, 25]]) {
        assert.throws(() => sizeMessage(cols, rows), RangeError);
    }
});
