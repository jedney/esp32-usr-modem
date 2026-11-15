// A device reset can leave TCP half-open. Keep transport lifecycle outside the DOM.
import {reconnectDelay, parseMessage, terminalAccess} from './status.js';
export const BUSY_DELAY = 30000;
export function createModemLink({session, socket, receive, status, disconnect,
    now = () => performance.now(), schedule = setTimeout, cancel = clearTimeout}) {
    let current = null, timer = null, attempt = 0, generation = 0, seen = 0, stopped = false;
    let owned = false, paused = false, inputTimer = null, queue = [], queued = 0, offset = 0;
    const active = ws => ws === current && !stopped;
    const clearInput = () => {
        if (inputTimer !== null) cancel(inputTimer);
        inputTimer = null;
        queue = []; queued = 0; offset = 0;
    };
    const ready = () => current && current.readyState === 1 && !stopped;
    // Match the firmware's 512-byte reads and 2 KiB input queue. bufferedAmount
    // lets TCP backpressure reach this bounded page queue during a slow paste.
    const pumpInput = () => {
        inputTimer = null;
        if (!ready() || !owned || paused) { clearInput(); return; }
        if (queued && current.bufferedAmount < 2048) {
            const bytes = queue[0];
            const end = Math.min(offset + 512, bytes.length);
            current.send(bytes.subarray(offset, end));
            queued -= end - offset;
            offset = end;
            if (offset === bytes.length) { queue.shift(); offset = 0; }
        }
        if (queued) inputTimer = schedule(pumpInput, 10);
    };
    // busy: the modem answered "update in progress" (HTTP 503). Another tab is writing an
    // image, so ask rarely instead of looping every few seconds.
    const retry = (busy = false) => {
        if (stopped || timer !== null) return;
        status('offline');
        const delay = reconnectDelay(attempt++);
        timer = schedule(() => { timer = null; open(); }, busy ? Math.max(delay, BUSY_DELAY) : delay);
    };
    const reset = () => {
        generation++;
        owned = false;
        clearInput();
        const old = current;
        current = null;
        if (old) { old.onclose = null; old.close(); }
        disconnect();
    };
    const open = async () => {
        if (stopped) return;
        const tokenGeneration = ++generation;
        status('connecting');
        try {
            const token = await session();
            if (stopped || tokenGeneration !== generation) return;
            const ws = current = socket(token);
            seen = now();
            ws.onopen = () => {
                if (!active(ws)) return;
                attempt = 0;
                seen = now();
                status('online');
            };
            ws.onmessage = event => {
                if (!active(ws)) return;
                seen = now();
                if (typeof event.data === 'string') {
                    const msg = parseMessage(event.data);
                    if (msg?.t === 'access') {
                        owned = terminalAccess(msg);
                        if (!owned) clearInput();
                    }
                }
                receive(event.data);
            };
            ws.onclose = () => {
                if (!active(ws)) return;
                current = null;
                owned = false;
                clearInput();
                disconnect();
                retry();
            };
        } catch (error) {
            if (!stopped && tokenGeneration === generation) retry(error?.busy === true);
        }
    };
    return {
        start: open,
        send(data) {
            if (!ready() || !owned || paused) return false;
            if (typeof data === 'string') { current.send(data); return true; }
            if (!(data instanceof Uint8Array) || queued + data.length > 1024 * 1024) return false;
            if (!data.length) return true;
            queue.push(data.slice());
            queued += data.length;
            if (inputTimer === null) pumpInput();
            return true;
        },
        openTerminal() {
            if (!ready() || paused) return false;
            current.send('{"t":"open"}');
            return true;
        },
        closeTerminal() { reset(); return open(); },
        hangup() {
            if (!ready() || !owned || paused) return false;
            clearInput();
            current.send('{"t":"hangup"}');
            return true;
        },
        pause(value) { paused = value; if (paused) clearInput(); },
        reconnect() { reset(); retry(); },
        poll(paused = false) {
            if (!current || paused || stopped) return;
            if (now() - seen >= 15000) { reset(); retry(); return; }
            if (current.readyState === 1) current.send('{"t":"ping"}');
        },
        stop() {
            stopped = true;
            if (timer !== null) { cancel(timer); timer = null; }
            reset();
        },
    };
}
