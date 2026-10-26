// A device reset can leave TCP half-open. Keep transport lifecycle outside the DOM.
import {reconnectDelay} from './status.js';
export function createModemLink({session, socket, receive, status, disconnect,
    now = () => performance.now(), schedule = setTimeout, cancel = clearTimeout}) {
    let current = null, timer = null, attempt = 0, generation = 0, seen = 0, stopped = false;
    const active = ws => ws === current && !stopped;
    const retry = () => {
        if (stopped || timer !== null) return;
        status('offline');
        timer = schedule(() => { timer = null; open(); }, reconnectDelay(attempt++));
    };
    const reset = () => {
        generation++;
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
                receive(event.data);
            };
            ws.onclose = () => {
                if (!active(ws)) return;
                current = null;
                disconnect();
                retry();
            };
        } catch {
            if (!stopped && tokenGeneration === generation) retry();
        }
    };
    return {
        start: open,
        send(data) {
            if (!current || current.readyState !== 1 || stopped) return false;
            current.send(data);
            return true;
        },
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
