"""Generate illustrative wrapping lists. No device values are invented."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def generate():
    menu = json.loads((ROOT / 'menu-definition.json').read_text(encoding='utf-8'))['main_menu']
    bindings = json.loads((ROOT.parent / 'lib/config/ui-bindings.json').read_text(encoding='utf-8'))
    theme = json.loads((ROOT / 'theme-tokens.json').read_text(encoding='utf-8'))

    def listing(title, items):
        lines = [title, 'Illustration only: current values appear on the modem.', '']
        for item in items:
            m = bindings[item['id']]
            lines += [f"{item['key']}. {m['friendly']}"]
        return '\n'.join(lines + ['', 'X. Back / Q. Exit; Esc goes back.', ''])

    for model in theme['models']:
        (ROOT / 'previews' / f"main-{model['id']}.txt").write_text(
            listing(model['label'] + ' - Configuration', menu), encoding='utf-8', newline='\n')
    for section, name in [('settings', 'settings'), ('phonebook', 'phonebook'), ('hardware-tests', 'hardware-tests')]:
        parent = next(m for m in menu if m['id'] == section)
        (ROOT / 'previews' / f'{name}-28.8k.txt').write_text(
            listing(parent['label'], parent['items']), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    generate()
