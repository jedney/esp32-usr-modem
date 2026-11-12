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
test('an image with two different identities is refused', () => {
    const enc = text => new TextEncoder().encode(text);
    const build = (...markers) => {
        const body = markers.map(enc);
        const image = new Uint8Array(24 + body.reduce((n, m) => n + m.length, 0));
        image[0] = 0xe9; image[12] = 9;
        let at = 24;
        for (const m of body) { image.set(m, at); at += m.length; }
        return image;
    };
    // The same marker twice is fine; a different version or kind in the same file is not.
    assert.equal(inspectImage(build('USRMODEM/1/FIRMWARE/0.11\n', 'USRMODEM/1/FIRMWARE/0.11\n')).version, '0.11');
    assert.throws(() => inspectImage(build('USRMODEM/1/FIRMWARE/0.11\n', 'USRMODEM/1/FIRMWARE/0.10\n')), /Conflicting/);
    assert.throws(() => inspectImage(build('USRMODEM/1/FIRMWARE/0.11\n', 'USRMODEM/1/LITTLEFS/0.11\n')), /Conflicting/);
});
test('a filesystem image must be exactly 0x7e0000 bytes and a firmware image at most 4 MiB', () => {
    const fs = new Uint8Array(0x7e0000);
    fs.set(new TextEncoder().encode('USRMODEM/1/LITTLEFS/0.11\n'), 100);
    assert.equal(inspectImage(fs).target, 'littlefs');
    assert.throws(() => inspectImage(fs.subarray(0, 0x7dffff)), /filesystem size/);
    const app = new Uint8Array(0x400001);
    app[0] = 0xe9; app[12] = 9; app.set(new TextEncoder().encode('USRMODEM/1/FIRMWARE/0.11\n'), 24);
    assert.throws(() => inspectImage(app), /ESP32-S3/);
});
test('downgrade comparison uses numeric project versions', () => {
    assert.equal(compareVersions('0.11','0.9'),1);
    assert.equal(compareVersions('0.9','0.11'),-1);
    assert.equal(compareVersions('0.11','0.11'),0);
    assert.throws(() => compareVersions('0.11-dev','0.11'));
});
