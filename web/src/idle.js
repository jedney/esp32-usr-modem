// Timestamp-only policy. Incoming modem data never calls activity().
export function createIdleGuard({now = Date.now, prompt, hangup}) {
    let armed = false, expired = false, activeAt = 0, shown = null;
    const show = seconds => {
        if (shown === seconds) return;
        shown = seconds;
        prompt(seconds);
    };
    return {
        connected(value) {
            if (armed === value) return;
            armed = value;
            expired = false;
            activeAt = now();
            show(null);
        },
        activity() {
            if (!armed || expired) return;
            activeAt = now();
            show(null);
        },
        poll() {
            if (!armed || expired) return;
            const remaining = activeAt + 300000 - now();
            if (remaining <= 0) {
                expired = true;
                show(null);
                hangup();
            } else if (remaining <= 60000) show(Math.ceil(remaining / 1000));
        },
    };
}
