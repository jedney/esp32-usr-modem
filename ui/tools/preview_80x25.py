#!/usr/bin/env python3
"""Reference 80x25 cell layout generator; not a production modem UI.

Produces exact 80x25 plain-text and ANSI samples from ui JSON. Does not perform
any AT commands or device actions; the preview's status is explicitly unknown.
Run from anywhere: python ui/tools/preview_80x25.py
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Optional
import json, unicodedata

ROOT=Path(__file__).resolve().parents[1]
W,H=80,25
DATA=json.loads((ROOT/'menu-definition.json').read_text(encoding="utf-8"))
THEME=json.loads((ROOT/'theme-tokens.json').read_text(encoding="utf-8"))
COLORS=THEME['colors']

@dataclass
class Cell:
    ch: str = ' '
    fg: str = COLORS['primary_text']
    bg: str = COLORS['panel_background']

class Frame:
    def __init__(self, model):
        self.model = model
        self.cells = [[Cell() for _ in range(W)] for _ in range(H)]
    def put(self,x,y,s,fg=None,bg=None):
        for i,ch in enumerate(s):
            if 0 <= x+i < W and 0 <= y < H:
                self.cells[y][x+i]=Cell(ch,fg or COLORS['primary_text'],bg or COLORS['panel_background'])
    def center(self,y,s,fg=None):
        self.put(max(1,(W-len(s))//2),y,s,fg)
    def rule(self,y,title=None):
        self.put(0,y,'╠'+('═'*(W-2))+'╣',COLORS['frame_white'])
        if title:
            text=f' [ {title} ] '
            self.center(y,text,COLORS['bright_cyan'])
    def border(self):
        self.put(0,0,'╔'+('═'*(W-2))+'╗',COLORS['frame_white'])
        self.put(0,H-1,'╚'+('═'*(W-2))+'╝',COLORS['frame_white'])
        for y in range(1,H-1):
            self.put(0,y,'║',COLORS['frame_white'])
            self.put(W-1,y,'║',COLORS['frame_white'])
    def lines(self):
        return [''.join(c.ch for c in row) for row in self.cells]
    def txt(self):
        return '\n'.join(self.lines())+'\n'
    def ans(self):
        def seq(fg,bg):
            def rgb(c):
                return tuple(int(c[i:i+2],16) for i in (1,3,5))
            return '\x1b[38;2;{};{};{};48;2;{};{};{}m'.format(*(rgb(fg)+rgb(bg)))
        out=['\x1b[0m\x1b[2J\x1b[H']
        for y,row in enumerate(self.cells):
            out.append(f'\x1b[{y+1};1H')
            prev=None
            for c in row:
                key=(c.fg,c.bg)
                if key!=prev:
                    out.append(seq(*key));prev=key
                out.append(c.ch)
        out.append('\x1b[0m')
        return ''.join(out)

# Compact, cell-width-safe 5x5 USRobotics character art. Red wordmark, 59 cols.
LOGO = {
'U':['█   █','█   █','█   █','█   █','█████'],
'S':['█████','█    ','█████','    █','█████'],
'R':['████ ','█   █','████ ','█  █ ','█   █'],
'o':['     ',' ███ ','█   █','█   █',' ███ '],
'b':['█    ','█    ','████ ','█   █','████ '],
't':['  █  ','█████','  █  ','  █  ','  ██ '],
'i':['  █  ','     ','  █  ','  █  ','  █  '],
'c':['     ',' ████','█    ','█    ',' ████'],
's':['     ',' ████','██   ','   ██','████ ']
}

def large_logo(f):
    word='USRobotics'
    width=len(word)*6-1
    x=(W-width)//2
    for row in range(5):
        line=' '.join(LOGO[ch][row] for ch in word)
        f.put(x,row+1,line,COLORS['logo_red'])

def mini_header(f,menu):
    f.put(3,1,'USRobotics',COLORS['logo_red'])
    f.put(23,1,f.model['label'],f.model['accent'])
    f.put(2,2,'─'*76,COLORS['frame_white'])
    f.put(3,3,'MAIN  >  '+menu.upper(),COLORS['bright_cyan'])
    f.put(3,4,'KEY   OPTION',COLORS['frame_white'])


def footer(f,select_msg,mode='MAIN'):
    if mode=='MAIN':
        f.rule(19,'HELP / DEVICE STATUS')
        f.put(3,20,'F1 Help  F2 Status  F3 Phonebook  F4 Settings  F10 Exit',COLORS['shortcut_blue'])
        f.put(3,21,'Model: '+f.model['short_label']+'  |  Profile: --  |  Link: UNKNOWN',COLORS['primary_text'])
        f.put(3,22,select_msg,COLORS['muted_text'])
    else:
        f.put(3,20,'UP/DOWN Select  ENTER Open  ESC Back  Q Back',COLORS['shortcut_blue'])
        f.put(3,21,'F1 Help  F2 Main  |  Model: '+f.model['short_label']+'  Link: UNKNOWN',COLORS['muted_text'])
        f.put(3,22,select_msg,COLORS['muted_text'])
    f.put(3,23,'Command >',COLORS['bright_cyan'])
    f.put(13,23,'█',COLORS['frame_white'])

def menu_line(f,y,x,key,label,disabled=False,selected=False,maxwidth=36):
    if disabled:
        s=f'[{key}] {label} [X]'
        f.put(x,y,s[:maxwidth],COLORS['disabled_gray'])
    else:
        f.put(x,y,'>' if selected else ' ',COLORS['warning_yellow'])
        f.put(x+2,y,f'[{key}]',COLORS['hotkey_green'])
        f.put(x+6,y,label[:maxwidth-7],COLORS['bright_cyan'], COLORS['highlight_text'] if selected else None)

def main(f):
    f.border();large_logo(f)
    f.center(6,'─── '+f.model['label']+' ───',f.model['accent'])
    f.rule(7,'MODEM CONFIGURATION - MAIN MENU')
    items=DATA['main_menu']
    for n,y in enumerate(range(8,15)):
        if n<6:
            a=items[n]
            menu_line(f,y,2,a['key'],a['label'],selected=(n==0),maxwidth=36)
        a=items[6+n]
        menu_line(f,y,41,a['key'],a['label'],disabled=a.get('status')=='unavailable',maxwidth=36)
        f.put(39,y,'│',COLORS['frame_white'])
    f.put(2,15,' ' * 76)
    f.rule(16)
    f.put(3,17,'ARROWS: navigate / switch column    ENTER: select    Q: quit',COLORS['shortcut_blue'])
    f.put(3,18,'[C] Client Tests - serial client diagnostics',COLORS['disabled_gray'])
    footer(f,'Reference layout only. Device information is not connected.')
    return f

def submenu(f,key):
    entry=next(m for m in DATA['main_menu'] if m['id']==key)
    f.border();mini_header(f,entry['label'])
    items=entry['items']
    for i,it in enumerate(items[:15]):
        y=5+i
        f.put(3,y,'>' if i==0 else ' ',COLORS['warning_yellow'])
        f.put(6,y,f"[{it['key']}]",COLORS['hotkey_green'])
        f.put(12,y,it['label'],COLORS['bright_cyan'])
    if len(items)<15:
        f.put(6,5+len(items),'No further options',COLORS['muted_text'])
    footer(f,'All actions depend on existing project services.',mode='SUB')
    return f

def phonebook(f):
    f.border()
    f.put(3,1,'USRobotics',COLORS['logo_red'])
    f.put(23,1,f.model['label'],f.model['accent'])
    f.rule(2,'MAIN > PHONEBOOK')
    f.put(3,3,'PHONEBOOK ENTRIES                      PAGE 1 / 1',COLORS['bright_cyan'])
    f.put(3,4,'#    NAME                       PHONE NUMBER          RATE',COLORS['frame_white'])
    f.put(2,5,'─'*76,COLORS['frame_white'])
    f.put(5,7,'No saved entries loaded',COLORS['muted_text'])
    f.put(5,8,'(Connect existing phonebook service to show live entries.)',COLORS['muted_text'])
    f.put(5,9,'List uses rows 7-15 for up to 9 entries per page.',COLORS['muted_text'])
    f.rule(15,'SELECTED ENTRY')
    f.put(3,16,'Name: --      Number: --      Speed: --',COLORS['primary_text'])
    f.put(3,17,'[I] Full details   |  [1] List   [2] Add / edit',COLORS['bright_cyan'])
    f.put(3,18,'[3] Delete (requires entry)   [4] Dial (requires entry)',COLORS['disabled_gray'])
    f.rule(19,'NAVIGATION')
    f.put(3,20,'UP/DOWN Select  PGUP/PGDN Pages  ENTER Details  ESC Back',COLORS['shortcut_blue'])
    f.put(3,21,'F1 Help  F2 Main  |  Model: '+f.model['short_label']+'  Link: UNKNOWN',COLORS['muted_text'])
    f.put(3,22,'Status: Unbound phonebook preview; no sample numbers.',COLORS['muted_text'])
    f.put(3,23,'Command >',COLORS['bright_cyan']);f.put(13,23,'█',COLORS['frame_white'])
    return f

def write(name,f):
    (ROOT/'previews'/f'{name}.txt').write_text(f.txt(),encoding='utf-8')
    (ROOT/'previews'/f'{name}.ans').write_text(f.ans(),encoding='utf-8')
    return f

def generate():
    frames={}
    for m in THEME['models']:
        frames[m['id']]=write('main-'+m['id'],main(Frame(m)))
    m=THEME['models'][1]
    frames['phonebook']=write('phonebook-28.8k',phonebook(Frame(m)))
    frames['settings']=write('settings-28.8k',submenu(Frame(m),'settings'))
    frames['hardware']=write('hardware-tests-28.8k',submenu(Frame(m),'hardware-tests'))
    return frames

if __name__=='__main__':
    frames=generate()
    for label,f in frames.items():
        lines=f.lines()
        assert len(lines)==25 and all(len(x)==80 for x in lines),label
        assert all(unicodedata.east_asian_width(c) not in ['W','F'] for line in lines for c in line),label
    print('Generated',len(frames),'80x25 TXT/ANSI preview pairs.')
    print(frames['sportster-28.8k'].txt())
