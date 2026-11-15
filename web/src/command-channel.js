// Firmware acknowledgements include consumed input bytes and follow the command's output.
// This works with quiet/numeric modem replies and never guesses readiness from OK text.
export function createCommandChannel({send, changed, show}) {
    let tx = 0, rx = 0, idle = false, capture = null;
    let errors = null, failureSeen = false, tail = '';
    const busy = () => !idle || rx !== tx;
    const finish = text => {
        const pending = capture; capture = null;
        pending?.done(text);
    };
    const input = bytes => {
        const previous = tx;
        tx = (tx + bytes.length) >>> 0;
        changed();
        if (send(bytes)) return true;
        tx = previous; changed();
        return false;
    };
    return {
        busy, input,
        request(line, done) {
            if (busy()) { show('Finish the current command in the terminal first.'); return false; }
            if (done) capture = {done, text:'', decoder:new TextDecoder(), overflow:false};
            if (input(new TextEncoder().encode(line))) return true;
            finish(null);
            show('Could not send the command. Check the modem link and try again.');
            return false;
        },
        output(bytes) {
            for (const ch of new TextDecoder().decode(bytes)) {
                if (ch === '\r' || ch === '\n') {
                    if (tail === 'ERROR' || tail === '4') failureSeen = true;
                    tail = '';
                } else tail = (tail + ch).slice(-6);
            }
            if (!capture) return;
            if (capture.text.length + bytes.length > 16384) { capture.overflow = true; return; }
            capture.text += capture.decoder.decode(bytes, {stream:true});
        },
        ready(message) {
            if (!Number.isInteger(message.rx) || message.rx < 0 || message.rx > 0xffffffff) return;
            rx = message.rx; idle = message.idle === true;
            const failed = Number.isInteger(message.errors) && errors !== null && errors !== message.errors;
            if (Number.isInteger(message.errors)) errors = message.errors;
            if (failed && !failureSeen) show('Command refused');
            if (failed && capture) finish(null);
            changed();
            if (!busy() && capture) {
                if (capture.overflow) { show('The command reply is too large. Read it in the terminal.'); finish(null); }
                else finish(capture.text);
            }
            if (!busy()) { failureSeen = false; tail = ''; }
        },
        reset() { tx = rx = 0; idle = false; errors = null; tail = ''; failureSeen = false; finish(null); changed(); },
    };
}
