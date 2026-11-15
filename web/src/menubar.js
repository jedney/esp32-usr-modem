import commands from './ui-commands.js';

// Geometry and text come from the same catalogue as AT*CONFIG. Client tests are serial only.
const icons = {
    status: 'activity', phonebook: 'book', settings: 'sliders', sounds: 'sound',
    debug: 'pulse', 'call-history': 'history', 'wifi-clock': 'wifi',
    'updates-reset': 'upload', 'export-configuration': 'download', about: 'info',
    'hardware-tests': 'chip', 'command-reference': 'terminal', 'settings-group':'sliders', help:'info', clock:'history', reset:'pulse', github:'info',
};

export function menuSections() {
    const section = command => ({...command, icon:icons[command.id] || 'sliders',
        items: command.template ? [] : commands.filter(c => c.screen === command.target && c.id !== 'client-tests').map(section)});
    return commands.filter(c => c.screen === 'Main').map(section);
}

export function menuDisabledReason(state) {
    if (!state.online) return 'Connect to the modem to use these controls.';
    if (!state.edit) return 'Click the terminal or select a menu item to reconnect.';
    if (state.upload) return 'Wait for the image upload to finish.';
    if (state.call && ['connected','dialling','ringing','ppp','slip'].includes(state.call.state)) return 'Hang up the call to change modem settings.';
    if (state.hardware?.active || (state.call && ['test','config'].includes(state.call.state)))
        return 'Finish the current test or configuration list in the terminal first.';
    if (state.busy) return 'Finish the current command in the terminal first.';
    return '';
}

// Widths include each item's gap. Keep the configuration order as the window narrows.
export function visibleMenuCount(width, itemWidths, moreWidth) {
    if (itemWidths.reduce((sum, item) => sum + item, 0) <= width) return itemWidths.length;
    let used = moreWidth, count = 0;
    for (const item of itemWidths) {
        if (used + item > width) break;
        used += item;
        count++;
    }
    return count;
}

export function commandForm(command, choice) {
    const options = command.variants || (!command.template ? command.fields.filter(f => !f.readOnly) : null);
    const selected = options?.find(v => v.id === choice) || options?.[0] || command;
    const fields = selected.fields ?? (selected.template?.includes('{value}') ? [{...selected, id: 'value'}] : []);
    return {...command, ...selected, commandId: command.id, fields, choices: options};
}

function invalid(field, message) {
    const error = new Error(message);
    error.field = field.id;
    throw error;
}
function ipv4(value) {
    if (!/^(?:\d{1,3}\.){3}\d{1,3}$/.test(value)) return null;
    const parts = value.split('.').map(Number);
    return parts.some(n => n > 255) ? null : parts.reduce((ip, n) => (ip * 256 + n) >>> 0, 0);
}
function fieldValue(field, raw) {
    const value = String(raw ?? '');
    if (value === '' && field.optional) return '';
    if (/[\x00-\x1f\x7f]/.test(value)) invalid(field, 'Use text without control characters or line breaks.');
    if (field.values && !field.allowCustom && !field.values.some(v => v.value === value))
        invalid(field, 'Choose one of the listed options.');
    if (field.type === 'int') {
        const scale = field.displayScale ?? 1;
        const wire = Number(value) / scale;
        if (!value.trim() || !Number.isFinite(wire) || Math.abs(wire - Math.round(wire)) > 1e-7)
            invalid(field, `Enter a number in steps of ${scale} ${field.displayUnit || field.unit || ''}.`);
        if (wire < field.min || wire > field.max)
            invalid(field, `Enter a value from ${field.min * scale} to ${field.max * scale} ${field.displayUnit || field.unit || ''}.`);
        return String(Math.round(wire));
    }
    const length = new TextEncoder().encode(value).length;
    if (field.minLength !== undefined && length < field.minLength)
        invalid(field, `Enter at least ${field.minLength} ${field.unit || 'characters'}.`);
    if (field.maxLength !== undefined && length > field.maxLength)
        invalid(field, `Use no more than ${field.maxLength} ${field.unit || 'characters'}.`);
    if (field.pattern && !new RegExp(`^(?:${field.pattern})$`, 'u').test(value))
        invalid(field, `Enter a valid ${field.friendly.toLowerCase()}. ${field.help}`);
    if (field.type === 'ip' && ipv4(value) === null) invalid(field, 'Enter an address such as 192.168.1.10.');
    const acceptsEmpty = field.minLength === 0 || (field.pattern && new RegExp(`^(?:${field.pattern})$`, 'u').test(''));
    if (!value && !field.optional && !acceptsEmpty) invalid(field, 'Enter a value before continuing.');
    return value;
}

export function buildCommand(form, values = {}) {
    const wire = Object.fromEntries(form.fields.map(field => [field.id, fieldValue(field, values[field.id])]));
    if (form.commandId === 'wifi-clock.ip-configuration' && form.id === 'static') {
        const ip = ipv4(wire.ip), mask = ipv4(wire.mask), gateway = ipv4(wire.gateway);
        const hosts = (~mask) >>> 0;
        if (!mask || hosts < 3 || (hosts & (hosts + 1)) !== 0 ||
            (ip & mask) !== (gateway & mask) || ip === gateway ||
            (ip & hosts) === 0 || (ip & hosts) === hosts || (gateway & hosts) === 0 || (gateway & hosts) === hosts)
            invalid({id: 'mask'}, 'Use a valid subnet mask and different device and gateway addresses in the same subnet.');
    }
    if (form.commandId === 'wifi-clock.internet-settings' && form.id === 'ip') {
        const local = ipv4(wire.local), peer = ipv4(wire.peer);
        const privateIp = ip => (ip >>> 24) === 10 || (ip >>> 20) === 0xac1 || (ip >>> 16) === 0xc0a8;
        if (!privateIp(local) || !privateIp(peer) || (local & ~3) !== (peer & ~3) ||
            local === peer || ![1,2].includes(local & 3) || ![1,2].includes(peer & 3))
            invalid({id:'local'}, 'Use the two different host addresses of the same private /30 subnet.');
    }
    let template = form.template;
    for (const field of [...form.fields].reverse()) {
        const suffix = `,{${field.id}}`;
        if (field.optional && wire[field.id] === '' && template.endsWith(suffix)) template = template.slice(0, -suffix.length);
    }
    const line = template.replace(/\{([^}]+)\}/g, (_, id) => wire[id] ?? '');
    if (!line || /[\r\n]/.test(line) || new TextEncoder().encode(line).length > 128)
        throw new Error('This command is too long. Shorten the entered values.');
    return `AT${line}\r`;
}

export function parsePhonebook(text) {
    return text.split(/[\r\n]+/).flatMap(line => {
        const m = /^(\d{1,11}),([A-Za-z0-9._-]{1,63}),(\d{1,5}),(TELNET|RAW),(\d{1,6})$/.exec(line);
        return m && Number(m[3]) > 0 && Number(m[3]) <= 65535
            ? [{num:m[1], host:m[2], port:m[3], proto:m[4], speed:m[5]}] : [];
    }).slice(0, 64);
}

export function parseNetworks(text) {
    return text.split(/[\r\n]+/).flatMap(line => {
        const m = /^\s*(\d+)\s{2}(.+?)\s+(-\d+) dBm\s+ch\s+\d+\s+(.+)$/.exec(line);
        return m && Number(m[1]) >= 1 && Number(m[1]) <= 20
            ? [{value:m[1], label:`${m[2].trim()} · ${m[3]} dBm`, description:m[4]}] : [];
    });
}
