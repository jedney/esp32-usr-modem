import assert from 'node:assert/strict';
import test from 'node:test';
import {terminalGeometry, sizeMessage} from '../src/geometry.js';

test('adaptive geometry retains compact, intermediate and wide screens within bounds', () => {
    assert.deepEqual(terminalGeometry(400, 300, 10, 20), {cols: 80, rows: 25});
    assert.deepEqual(terminalGeometry(1000, 500, 10, 20), {cols: 100, rows: 25});
    assert.deepEqual(terminalGeometry(1200, 900, 10, 20), {cols: 120, rows: 45});
    assert.deepEqual(terminalGeometry(3000, 2000, 10, 20), {cols: 132, rows: 50});
    assert.deepEqual(terminalGeometry(0, 500, 10, 20), {cols: 80, rows: 25});
    assert.deepEqual(terminalGeometry(1000, 500, 0, 20), {cols: 80, rows: 25});
});
test('geometry messages have canonical ordering and enforce device bounds', () => {
    assert.equal(sizeMessage(120, 45), '{"t":"size","cols":120,"rows":45}');
    for (const [cols, rows] of [[0, 25], [133, 25], [80, 0], [80, 51], [80.5, 25], [NaN, 25]]) {
        assert.throws(() => sizeMessage(cols, rows), RangeError);
    }
});
