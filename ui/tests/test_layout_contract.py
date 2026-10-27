"""Static handoff contract; this cannot test the consuming app itself."""
import unittest, json, importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
menu=json.loads((ROOT/'menu-definition.json').read_text(encoding="utf-8"))
theme=json.loads((ROOT/'theme-tokens.json').read_text(encoding="utf-8"))

class HandoffContract(unittest.TestCase):
    def test_grid_dimensions_and_widths(self):
        import unicodedata
        files=list((ROOT/'previews').glob('*.txt'))
        self.assertEqual(len(files),7)
        for p in files:
            with self.subTest(file=p.name):
                lines=p.read_text(encoding="utf-8").splitlines()
                self.assertEqual(len(lines),25)
                self.assertTrue(all(len(line)==80 for line in lines))
                self.assertTrue(all(unicodedata.east_asian_width(c) not in ('W','F') for line in lines for c in line))
    def test_four_exact_models(self):
        expected={'sportster-14.4k':'#6f327f','sportster-28.8k':'#448FA1','sportster-33.6k':'#349b67','sportster-56k':'#3F668E'}
        self.assertEqual({m['id']:m['accent'] for m in theme['models']},expected)
        self.assertEqual(theme['active_model_source'].split(' ')[0],'AT*MODEL')
        for m in theme['models']:
            s=(ROOT/'previews'/f"main-{m['id']}.txt").read_text(encoding="utf-8")
            self.assertIn(m['label'],s)
    def test_inventory(self):
        items=menu['main_menu']
        self.assertEqual(len(items),13)
        self.assertEqual(sum(len(m['items']) for m in items),77)
        self.assertEqual([m['key'] for m in items],list('123456789ABCD'))
        self.assertEqual([m['label'] for m in items[-4:]],
                         ['About','Hardware Tests','Client Tests','Command Reference'])
        self.assertEqual(len(set(m['id'] for m in items)),13)
        self.assertEqual(len(set(i['id'] for m in items for i in m['items'])),77)
        self.assertEqual([len(m['items']) for m in items],[0,4,13,4,4,0,15,7,0,0,15,15,0])
    def test_disabled_version(self):
        m=next(item for item in menu['main_menu'] if item['id']=='client-tests')
        self.assertEqual(m['label'],'Client Tests')
        self.assertEqual(m['status'],'available')
        for p in (ROOT/'previews').glob('main-*.txt'):
            self.assertIn('Client Tests',p.read_text(encoding="utf-8"))
    def test_submenu_keys(self):
        for m in menu['main_menu']:
            keys=[i['key'] for i in m['items']]
            self.assertEqual(len(keys),len(set(keys)))
            self.assertTrue(all(k not in ['Q','I'] for k in keys))
            self.assertLessEqual(len(keys),15)

if __name__=='__main__': unittest.main()
