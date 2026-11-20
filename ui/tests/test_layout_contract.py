"""Static handoff contract; this cannot test the consuming app itself."""
import unittest, json, importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
menu=json.loads((ROOT/'menu-definition.json').read_text(encoding="utf-8"))
def walk(nodes):
    return [item for node in nodes for item in [node, *walk(node.get('items', []))]]

theme=json.loads((ROOT/'theme-tokens.json').read_text(encoding="utf-8"))

class HandoffContract(unittest.TestCase):
    def test_typed_metadata_covers_the_whole_inventory(self):
        bindings = json.loads((ROOT.parent/'lib/config/ui-bindings.json').read_text(encoding='utf-8'))
        items = walk(menu['main_menu'])
        self.assertEqual(set(bindings), {i['id'] for i in items})
        for item in items:
            with self.subTest(id=item['id']):
                binding = bindings[item['id']]
                self.assertTrue(binding['friendly'].strip())
                self.assertTrue(binding['help'].strip())
                self.assertIn(binding['type'], {'enum', 'int', 'string', 'bool', 'ip', 'action', 'secret'})
                self.assertIn('template', binding)
                if item.get('items'):
                    self.assertEqual(binding['type'], 'action')
    def test_previews_are_wrapping_ascii_lists(self):
        for p in (ROOT/'previews').glob('*.txt'):
            text=p.read_text(encoding='utf-8')
            self.assertTrue(text.isascii())
            self.assertNotIn('ARROWS',text)
            self.assertIn('X. Back',text)
            self.assertIn('A. ',text)
            self.assertFalse(any(line.startswith('   ') for line in text.splitlines()))
    def test_four_exact_models(self):
        expected={'sportster-14.4k':'#6f327f','sportster-28.8k':'#448FA1','sportster-33.6k':'#349b67','sportster-56k':'#3F668E'}
        self.assertEqual({m['id']:m['accent'] for m in theme['models']},expected)
        self.assertEqual(theme['active_model_source'].split('=')[0],'AT*MODEL')
        for m in theme['models']:
            s=(ROOT/'previews'/f"main-{m['id']}.txt").read_text(encoding="utf-8")
            self.assertIn(m['label'],s)
    def test_inventory(self):
        roots=menu['main_menu']; items=walk(roots)
        self.assertEqual([n['label'] for n in roots], ['Status','Phone Book','Call History','Settings','Help'])
        self.assertEqual([n['key'] for n in roots], list('ABCDE'))
        self.assertEqual(len(items),96)
        self.assertEqual(len(set(n['id'] for n in items)),96)
        self.assertEqual([n['label'] for n in roots[3]['items']], ['General Settings','Sounds','Wi-Fi','Clock','Updates','Reset','Export Configuration'])
        self.assertEqual([n['label'] for n in roots[4]['items']], ['About','GitHub','Command Reference','Diagnostics'])
    def test_disabled_version(self):
        m=next(item for item in walk(menu['main_menu']) if item['id']=='client-tests')
        self.assertEqual(m['label'],'Client Tests')
        self.assertEqual(m['status'],'available')
        for p in (ROOT/'previews').glob('main-*.txt'):
            self.assertIn('Help',p.read_text(encoding="utf-8"))
    def test_submenu_keys(self):
        for m in walk(menu['main_menu']):
            keys=[i['key'] for i in m.get('items', [])]
            self.assertEqual(len(keys),len(set(keys)))
            self.assertEqual(keys,list('ABCDEFGHIJKLMNO')[:len(keys)])
            self.assertLessEqual(len(keys),15)

if __name__=='__main__': unittest.main()
