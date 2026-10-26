import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {sha256, inspectImage, compareVersions} from '../src/upload.js';
test('SHA-256 matches independent standard implementation across blocks', () => {
    for (const size of [0,1,3,55,56,63,64,65,1024,65537]) {
        const bytes = Uint8Array.from({length:size}, (_, i) => i & 255);
        assert.equal(sha256(bytes), createHash('sha256').update(bytes).digest('hex'));
    }
});
test('raw project image preflight checks target, chip and conflicting identity', () => {
    const marker = new TextEncoder().encode('USRMODEM/1/FIRMWARE/0.11\n');
    const image = new Uint8Array(24 + marker.length);
    image[0] = 0xe9; image[12] = 9; image.set(marker, 24);
    assert.equal(inspectImage(image).target, 'firmware');
    assert.equal(inspectImage(image).version, '0.11');
    image[12] = 0;
    assert.throws(() => inspectImage(image), /ESP32-S3/);
    assert.throws(() => inspectImage(new Uint8Array(24)), /identity/);
});
test('downgrade comparison uses numeric project versions', () => {
    assert.equal(compareVersions('0.11','0.9'),1);
    assert.equal(compareVersions('0.9','0.11'),-1);
    assert.equal(compareVersions('0.11','0.11'),0);
    assert.throws(() => compareVersions('0.11-dev','0.11'));
});
