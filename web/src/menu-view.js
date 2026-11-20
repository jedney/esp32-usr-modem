// DOM presentation for the shared command catalogue. Device replies stay in the terminal.
import commands from './ui-commands.js';
import {menuSections, menuDisabledReason, visibleMenuCount, commandForm, buildCommand, parsePhonebook, parseNetworks} from './menubar.js';

export function createMenuBar({state, run, focus, notify, ensureTerminal = action => action()}) {

const $ = id => document.getElementById(id);
const PROTOCOLS = {TELNET: 'Telnet', RAW: 'Raw TCP', SSH: 'SSH'};
const withTerminal = action => {
    if (state().edit) action();
    else ensureTerminal(action);
};
const paths = {
    activity: '<path d="M3 12h4l3-7 4 14 3-7h4"/>',
    book: '<path d="M5 3h14v18H5zM8 3v18M11 8h5M11 12h5"/>',
    sliders: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/>',
    sound: '<path d="M4 10h4l5-5v14l-5-5H4zM17 8a6 6 0 010 8M20 5a10 10 0 010 14"/>',
    pulse: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M5 12h4l2-5 3 10 2-5h3"/>',
    history: '<path d="M3 10a9 9 0 119 11M3 4v6h6M12 7v5l3 2"/>',
    wifi: '<path d="M3 8a14 14 0 0118 0M6 12a9 9 0 0112 0M9 16a4 4 0 016 0"/><circle cx="12" cy="20" r=".5"/>',
    upload: '<path d="M12 16V3M7 8l5-5 5 5M4 15v6h16v-6"/>',
    download: '<path d="M12 3v13M7 11l5 5 5-5M4 17v4h16v-4"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>',
    chip: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4M10 10h4v4h-4z"/>',
    terminal: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 9l3 3-3 3M13 15h4"/>',
    logout: '<path d="M10 4H5v16h5M14 8l4 4-4 4M18 12H9"/>',
};
function icon(name) {
    return `<svg class="icon" viewBox="0 0 24 24" aria-hidden="true">${paths[name] || paths.sliders}</svg>`;
}
function element(tag, className, text) {
    const el = document.createElement(tag);
    if (className) el.className = className;
    if (text !== undefined) el.textContent = text;
    return el;
}
function closeMenus() {
    for (const popup of document.querySelectorAll('.menu-popup')) popup.hidden = true;
    for (const button of document.querySelectorAll('.menu-trigger')) button.setAttribute('aria-expanded', 'false');
}
function openPopup(popup, anchor) {
    const open = popup.hidden;
    closeMenus();
    if (!open) return;
    const bounds = anchor.getBoundingClientRect();
    popup.style.left = `${Math.max(16, Math.min(bounds.left, innerWidth - Math.min(365, innerWidth - 32) - 16))}px`;
    popup.style.top = `${bounds.bottom + 7}px`;
    popup.style.maxHeight = `${Math.max(120, innerHeight - bounds.bottom - 23)}px`;
    popup.hidden = false;
    anchor.setAttribute('aria-expanded', 'true');
    anchor.setAttribute('aria-controls', popup.id);
    [...popup.querySelectorAll('button')].find(button => !button.disabled && !button.hidden && !button.closest('[hidden]'))?.focus();
}
let secretTimer = null, dialogGeneration = 0;
let networks = [];
const dialog = $('command-dialog');
function dismiss() {
    clearTimeout(secretTimer);
    for (const input of $('dialog-body').querySelectorAll('input')) input.value = '';
    $('dialog-body').replaceChildren();
    dialogGeneration++;
    dialog.close();
    focus();
}
dialog.addEventListener('cancel', event => { event.preventDefault(); dismiss(); });
$('dialog-cancel').onclick = dismiss;
function dialogHeader(command, section) {
    closeMenus();
    clearTimeout(secretTimer);
    dialogGeneration++;
    $('dialog-title').textContent = command.friendly;
    $('dialog-intro').textContent = command.help;
    $('dialog-icon').innerHTML = icon(section.icon);
    $('dialog-body').replaceChildren();
    $('dialog-apply').hidden = false;
    $('dialog-cancel').textContent = 'Cancel';
    $('dialog-apply').textContent = 'Run command';
    document.querySelector('.dialog-caption').textContent = 'Result appears in the terminal';
    if (!dialog.open) dialog.showModal();
}
function execute(command) {
    const reason = menuDisabledReason(state());
    if (reason) { notify(reason); return; }
    closeMenus();
    if (run(command, command === 'AT*WIFISCAN\r' ? reply => { networks = reply === null ? [] : parseNetworks(reply); } : undefined)) {
        if (command.startsWith('AT*HWBRIDGE')) notify('Use the terminal for the bridge. Press Ctrl-X to stop.');
        else if (/^AT\*(?:WIFISCAN|PLAY|HWLED|HWTONE|HWDTMF|HWPULSE|HWPOT)/.test(command))
            notify('Continue in the terminal. Press a key when prompted.');
        focus();
    }
}
function showDialog(command, section, initial = {}) {
    dialogHeader(command, section);
    let form = commandForm(command);
    const operation = form.choices ? element('div', 'field-group') : null;
    let operationSelect;
    if (operation) {
        const label = element('label', '', command.template ? 'Operation' : 'Setting');
        operationSelect = element('select'); operationSelect.id = 'command-operation'; label.htmlFor = operationSelect.id;
        for (const choice of form.choices) {
            const option = element('option', '', choice.friendly); option.value = choice.id;
            operationSelect.append(option);
        }
        const field = element('div', 'field'); field.append(label, operationSelect); operation.append(field);
        $('dialog-body').append(operation);
    }
    const group = element('div', 'field-group');
    $('dialog-body').append(group);
    const advanced = element('details', 'advanced');
    advanced.append(element('summary', '', 'Advanced · command preview'));
    const code = element('code'); advanced.append(code); $('dialog-body').append(advanced);
    const inputs = new Map();
    const errors = new Map();
    const values = () => Object.fromEntries([...inputs].map(([id, input]) => [id, input.value()]));
    const update = () => {
        if (command.secret) { code.textContent = 'AT*PASS=••••••••'; return; }
        try { code.textContent = buildCommand(form, values()).trimEnd(); }
        catch { code.textContent = 'Complete the fields to preview this command.'; }
    };
    function renderFields() {
      group.replaceChildren(element('div', 'field-group-title', command.friendly));
      inputs.clear(); errors.clear();
      group.hidden = form.fields.length === 0;
      for (const field of form.fields) {
        const wrap = element('div', 'field');
        const label = element('label', '', field.friendly);
        label.htmlFor = `field-${field.id}`;
        const choices = command.id === 'wifi-clock.join' && field.id === 'index' ? networks : field.values;
        const input = element(choices ? 'select' : 'input');
        input.id = label.htmlFor;
        input.setAttribute('aria-describedby', `${input.id}-help ${input.id}-error`);
        let custom;
        if (choices) {
            const placeholder = element('option', '', choices.length ? (field.optional ? 'Use the default' : 'Choose an option…') : 'Run Scan nearby Wi-Fi networks first');
            placeholder.value = ''; input.append(placeholder);
            const groups = new Map();
            for (const value of choices) {
                const option = element('option', '', value.label);
                option.value = value.value;
                option.title = value.description;
                if (command.id === 'wifi-clock.time-zone') {
                    const region = value.value.includes('/') ? value.value.split('/')[0] : 'Universal';
                    if (!groups.has(region)) {
                        const optgroup = element('optgroup'); optgroup.label = region;
                        groups.set(region, optgroup); input.append(optgroup);
                    }
                    groups.get(region).append(option);
                } else input.append(option);
            }
            if (field.allowCustom) {
                const option = element('option', '', 'Enter a custom value…'); option.value = '__custom'; input.append(option);
                custom = element('input'); custom.hidden = true; custom.type = 'text';
                custom.setAttribute('aria-label', `Custom ${field.friendly.toLowerCase()}`);
                custom.oninput = update;
            }
            if (choices.length > 16 || field.id === 'index') {
                const search = element('input'); search.type = 'search'; search.placeholder = 'Filter options…';
                search.setAttribute('aria-label', `Filter ${field.friendly.toLowerCase()}`);
                search.oninput = () => {
                    for (const option of input.querySelectorAll('option')) option.hidden = !!option.value && option.value !== '__custom'
                        && !option.textContent.toLowerCase().includes(search.value.toLowerCase());
                };
                wrap.append(search);
            }
        } else {
            input.type = field.type === 'secret' ? 'password' : field.type === 'int' ? 'number' : 'text';
            input.autocomplete = field.type === 'secret' ? 'new-password' : 'off';
            input.spellcheck = false;
            if (field.min !== undefined) input.min = field.min * (field.displayScale ?? 1);
            if (field.max !== undefined) input.max = field.max * (field.displayScale ?? 1);
            if (field.type === 'int') input.step = field.displayScale ?? 1;
            if (field.optional) input.placeholder = 'Optional';
        }
        input.value = String(initial[field.id] ?? '');
        inputs.set(field.id, {value: () => custom && input.value === '__custom' ? custom.value : input.value, input});
        input.oninput = () => {
            if (custom) custom.hidden = input.value !== '__custom';
            input.removeAttribute('aria-invalid');
            errors.get(field.id).textContent = '';
            update();
        };
        wrap.append(label);
        const holder = element('div', field.unit && !field.values ? 'field-input' : '');
        holder.append(input);
        if (field.unit && !choices) holder.append(element('span', 'field-unit', field.displayUnit || field.unit));
        wrap.append(holder);
        if (custom) wrap.append(custom);
        const help = element('small', '', field.help); help.id = `${input.id}-help`; wrap.append(help);
        const error = element('div', 'field-error'); error.id = `${input.id}-error`; error.setAttribute('aria-live', 'polite');
        wrap.append(error); errors.set(field.id, error);
        group.append(wrap);
      }
      update();
    }
    if (operationSelect) operationSelect.onchange = () => { form = commandForm(command, operationSelect.value); renderFields(); };
    renderFields();
    $('dialog-apply').textContent = 'Apply setting';
    $('dialog-apply').onclick = () => {
        const reason = menuDisabledReason(state());
        if (reason) { notify(reason); return; }
        try {
            const line = buildCommand(form, values());
            dismiss(); execute(line);
        } catch (error) {
            const target = errors.get(error.field);
            if (target) {
                target.textContent = error.message;
                inputs.get(error.field).input.setAttribute('aria-invalid', 'true');
                inputs.get(error.field).input.focus();
            } else notify(error.message);
        }
    };
    if (command.secret) secretTimer = setTimeout(dismiss, 120000);
    updateState();
}
function showPhonebook() {
    const command = commands.find(c => c.id === 'phonebook');
    dialogHeader(command, {icon:'book'});
    $('dialog-apply').hidden = true;
    $('dialog-cancel').textContent = 'Close';
    const generation = dialogGeneration;
    const tools = element('div', 'phonebook-tools');
    for (const [id, name] of [['phonebook.add-edit-entry','Add destination'], ['phonebook.dial','Dial an address']]) {
        const button = element('button', 'button requires-idle', name);
        button.onclick = () => { $('dialog-cancel').textContent = 'Cancel'; showDialog(commands.find(c => c.id === id), {icon:'book'}); };
        tools.append(button);
    }
    $('dialog-body').append(tools);
    const note = element('p', 'empty-note', 'Reading the phonebook…'); $('dialog-body').append(note);
    if (!run('AT*PHONEBOOK?\r', reply => {
      if (!dialog.open || generation !== dialogGeneration) return;
      if (reply === null || /(?:^|[\r\n])(?:ERROR|4)(?:[\r\n]|$)/.test(reply)) {
          note.textContent = 'Could not read the phonebook. Check the terminal for details.'; return;
      }
      const entries = parsePhonebook(reply);
    const scroll = element('div', 'table-scroll');
    const table = element('table');
    const head = element('thead');
    const hr = element('tr');
    for (const text of ['Number', 'Destination', 'Type', 'Actions']) {
        const th = element('th', '', text); th.scope = 'col'; hr.append(th);
    }
    head.append(hr); table.append(head);
    const body = element('tbody');
    for (const entry of entries) {
        const row = element('tr'); row.append(element('td', '', entry.num));
        row.append(element('td', '', `${entry.host}:${entry.port}`), element('td', '', PROTOCOLS[entry.proto] || entry.proto));
        const cell = element('td');
        for (const [name, action] of [
            ['Dial', () => { dismiss(); execute(`AT*DIAL=${entry.num}\r`); }],
            ['Edit', () => showDialog(commands.find(c => c.id === 'phonebook.add-edit-entry'), {icon:'book'}, entry)],
            ['Delete', () => showDialog(commands.find(c => c.id === 'phonebook.delete-entry'), {icon:'book'}, {value:entry.num})],
        ]) {
            const button = element('button', 'button requires-idle', name); button.onclick = action; cell.append(button);
        }
        row.append(cell); body.append(row);
    }
    table.append(body); scroll.append(table); $('dialog-body').append(scroll);
    note.textContent = entries.length ? `${entries.length} of 64 destinations` : 'Your phonebook is empty. Add a destination to save it for next time.';
    updateState();
    })) note.textContent = 'Could not read the phonebook. Open an idle terminal and try again.';
}
const menuEntries = [];
function choose(command, section) {
    if (command.url) { closeMenus(); window.open(command.url, '_blank', 'noopener,noreferrer'); return; }
    if (!state().edit) { ensureTerminal(() => choose(command, section)); return; }
    if (menuDisabledReason(state())) { notify(menuDisabledReason(state())); return; }
    if (command.id === 'phonebook.list-entries') { showPhonebook(); return; }
    if (command.input || !command.template) showDialog(command, section);
    else execute(buildCommand(commandForm(command)));
}
let menuAnchor = null;
function makePopup(section, parent = null) {
    const popup = element('div', 'menu-popup'); popup.id = `popup-${section.id}`; popup.hidden = true;
    popup.append(element('div', 'menu-heading', section.friendly));
    if (parent) {
        const back = element('button', 'menu-item', 'Back'); back.type = 'button'; back.dataset.back = 'true';
        back.onclick = () => openPopup(parent, menuAnchor); popup.append(back);
    }
    for (const item of section.items) {
        const button = element('button', 'menu-item'); button.type = 'button'; button.innerHTML = icon(item.icon || section.icon);
        button.title = item.help;
        const copy = element('span', 'menu-copy'); copy.append(element('span', 'menu-name', item.friendly));
        button.append(copy, element('span', 'menu-tag', item.items.length ? '>' : item.url ? 'Link' : item.input ? 'Edit...' : 'Run'));
        if (item.items.length) {
            button.dataset.section = item.id;
            const child = makePopup(item, popup);
            button.onclick = () => openPopup(child, menuAnchor);
        } else {
            if (item.url) button.dataset.link = 'true';
            button.onclick = () => choose(item, section);
        }
        popup.append(button);
    }
        if (section.id === 'updates-reset') {
            const upload = element('button', 'menu-item'); upload.id = 'upload'; upload.type = 'button';
            upload.title = 'Install a firmware or filesystem image from a release.';
            upload.innerHTML = icon('upload');
            const copy = element('span', 'menu-copy');
            copy.append(element('span', 'menu-name', 'Upload image…'));
            upload.append(copy);
            popup.append(upload);
        }
        if (section.id === 'debug') {
            const history = element('button', 'menu-item'); history.id = 'recent-console-history'; history.type = 'button';
            history.title = 'Show the newest console output since boot, up to 16 KB (1 KB if PSRAM is short).';
            history.innerHTML = icon('history');
            const copy = element('span', 'menu-copy');
            copy.append(element('span', 'menu-name', 'Recent console history'));
            history.append(copy);
            popup.append(history);
        }
        if (section.id === 'help') {
            // Browser-only and always available: observers, calls, prompts, tests and uploads.
            const logout = element('button', 'menu-item'); logout.id = 'logout'; logout.type = 'button';
            logout.dataset.always = 'true';
            logout.title = 'End this browser login in every tab. Other browsers stay logged in.';
            logout.innerHTML = icon('logout');
            const copy = element('span', 'menu-copy');
            copy.append(element('span', 'menu-name', 'Log out'));
            logout.append(copy);
            popup.append(logout);
        }
        if (section.id === 'hardware-tests') {
            const lines = element('div', 'line-controls'); lines.id = 'line-controls'; lines.hidden = true;
            for (const [id, name] of [['dcd','Toggle DCD'], ['ri','Toggle RI'], ['cts','Toggle CTS']]) {
                const button = element('button', 'button', name); button.id = id; lines.append(button);
            }
            popup.append(lines);
            const stop = element('button', 'menu-item'); stop.id = 'stop-test'; stop.hidden = true;
            stop.title = 'Finish the hardware test and return to commands.';
            stop.innerHTML = icon('chip');
            const copy = element('span', 'menu-copy');
            copy.append(element('span', 'menu-name', 'Stop test'));
            stop.append(copy, element('span', 'menu-tag', 'Ctrl-X'));
            popup.append(stop);
        }

    document.body.append(popup);
    return popup;
}
for (const section of menuSections()) {
    const host = element('div', 'menu-section');
    const trigger = element('button', 'menu-trigger'); trigger.type = 'button';
    trigger.innerHTML = icon(section.icon); trigger.append(element('span', '', section.friendly));
    trigger.title = section.help; host.append(trigger);
    let popup;
    if (section.items.length) {
        popup = makePopup(section);
        trigger.setAttribute('aria-expanded', 'false');
        trigger.setAttribute('aria-controls', popup.id);
        trigger.append(element('span', 'chevron', '⌄'));
        trigger.onclick = () => {
            if (section.id === 'phonebook') { withTerminal(showPhonebook); return; }
            menuAnchor = trigger; openPopup(popup, trigger);
        };
    } else trigger.onclick = () => choose(section, section);
    menuEntries.push({section, host, trigger, popup});
    $('menu').append(host);
}
const moreHost = element('div', 'menu-section');
const moreTrigger = element('button', 'menu-trigger', 'More');
moreTrigger.type = 'button';
moreTrigger.setAttribute('aria-expanded', 'false');
moreTrigger.setAttribute('aria-controls', 'popup-more');
moreTrigger.append(element('span', 'chevron', '⌄'));
moreHost.append(moreTrigger); $('menu').append(moreHost);
const morePopup = element('div', 'menu-popup'); morePopup.id = 'popup-more'; morePopup.hidden = true;
document.body.append(morePopup);
moreTrigger.onclick = () => openPopup(morePopup, moreTrigger);
function layoutMenu() {
    closeMenus();
    for (const {host} of menuEntries) host.hidden = false;
    moreHost.hidden = false;
    const bar = $('menu');
    const style = getComputedStyle(bar);
    const gap = parseFloat(style.columnGap) || 0;
    const available = bar.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
    const widths = menuEntries.map(({host}) => host.getBoundingClientRect().width + gap);
    const count = visibleMenuCount(available + gap, widths, moreHost.getBoundingClientRect().width + gap);
    moreHost.hidden = count === menuEntries.length;
    morePopup.replaceChildren(element('div', 'menu-heading', 'More modem controls'));
    menuEntries.forEach(({section, host, trigger, popup}, index) => {
        host.hidden = index >= count;
        if (index < count) return;
        const button = element('button', 'menu-item'); button.type = 'button';
        button.dataset.section = section.id;
        button.title = section.help;
        button.innerHTML = icon(section.icon);
        const copy = element('span', 'menu-copy');
        copy.append(element('span', 'menu-name', section.friendly));
        button.append(copy);
        if (popup && section.id !== 'phonebook') {
            button.append(element('span', 'menu-tag', '›'));
            button.onclick = () => {
                menuAnchor = moreTrigger; openPopup(popup, moreTrigger);
            };
        } else button.onclick = () => trigger.click();
        morePopup.append(button);
    });
    updateState();
}
new ResizeObserver(layoutMenu).observe($('menu'));
document.fonts.ready.then(layoutMenu);
document.addEventListener('click', event => { if (!event.target.closest('.menu-section, .menu-popup')) closeMenus(); });
document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
        const active = document.querySelector('.menu-trigger[aria-expanded="true"]');
        closeMenus(); active?.focus();
    }
});
function updateState() {
    const current = state(), reason = menuDisabledReason(current);
    const controls = current.online && current.edit && current.hardware?.active && !current.upload;
    const canRun = !current.edit ? !current.upload && !current.opening : !reason;
    const canOpen = section => !current.upload || section === 'help';
    for (const {section, trigger} of menuEntries) trigger.disabled = section.items.length && section.id !== 'phonebook' ? !canOpen(section.id) : !canRun;
    for (const button of document.querySelectorAll('.menu-popup .menu-item')) {
        button.disabled = button.id === 'stop-test' ? !controls
            : button.dataset.link || button.dataset.back || button.dataset.always ? false
            : button.dataset.section ? !canOpen(button.dataset.section) : !canRun;
    }
    for (const button of document.querySelectorAll('.requires-idle')) button.disabled = !!reason;
    $('line-controls').hidden = !controls || current.hardware.mode !== 'lines';
    $('stop-test').hidden = !controls;
    for (const button of $('line-controls').querySelectorAll('button')) button.disabled = !controls;
    $('dialog-apply').disabled = !!reason;
    moreTrigger.disabled = !menuEntries.some(entry => entry.host.hidden && canOpen(entry.section.id));
    if (!current.edit || !current.online) {
        if (dialog.open) dismiss();
    }
}
// Dropdowns use ordinary buttons and disclosure semantics; Tab and Escape remain native.
document.addEventListener('focusin', event => {
    if (!event.target.closest('.menu-section, .menu-popup') && !dialog.open) closeMenus();
});
// Before the login screen: close menus and dialogs and forget scanned networks.
function clear() {
    closeMenus();
    if (dialog.open) dismiss();
    networks = [];
}
return {update: updateState, clear};
}
