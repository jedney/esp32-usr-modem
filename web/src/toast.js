// Passive messages: safe text, no focus changes, no motion or blocking dialogs.
export function createToasts({container, document = globalThis.document,
    schedule = setTimeout, cancel = clearTimeout}) {
    const visible = [];
    return {
        show(text) {
            const el = document.createElement('div');
            el.className = 'toast';
            el.setAttribute('role', 'status');
            el.setAttribute('aria-atomic', 'true');
            el.textContent = text;
            container.appendChild(el);
            let timer;
            const dismiss = () => {
                const index = visible.indexOf(dismiss);
                if (index < 0) return;
                visible.splice(index, 1);
                cancel(timer);
                el.remove();
            };
            visible.push(dismiss);
            timer = schedule(dismiss, 5000);
            if (visible.length > 5) visible[0]();
            return dismiss;
        },
    };
}
