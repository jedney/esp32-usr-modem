#!/usr/bin/env python3
"""Render the generated fixed-cell TXT/ANSI specimens to accurately gridded PNGs.
This helper is only for documentation previews, not the application renderer.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from preview_80x25 import generate, COLORS
ROOT=Path(__file__).resolve().parents[1]
FONT=next(str(path) for path in (
    Path('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'),
    Path('C:/Windows/Fonts/consola.ttf'),
) if path.exists())
FONT_SZ=18
CELL_W,CELL_H=12,23
W,H=80,25
MARGIN=18

def rgb(value):
    return tuple(int(value[i:i+2],16) for i in (1,3,5))

def render(name,frame):
    image=Image.new('RGB',(80*CELL_W+2*MARGIN,25*CELL_H+2*MARGIN),(5,9,13))
    draw=ImageDraw.Draw(image)
    font=ImageFont.truetype(FONT,FONT_SZ)
    for y,row in enumerate(frame.cells):
        for x,c in enumerate(row):
            px=MARGIN+x*CELL_W
            py=MARGIN+y*CELL_H
            if c.bg != COLORS['panel_background']:
                draw.rectangle([px,py,px+CELL_W-1,py+CELL_H-1], fill=rgb(c.bg))
            # Cell-centered, no horizontal layout drift from glyph metrics
            bounds=draw.textbbox((0,0),c.ch,font=font)
            glyph_w=bounds[2]-bounds[0]
            draw.text((px+(CELL_W-glyph_w)//2-bounds[0],py+1),c.ch,font=font,fill=rgb(c.fg))
    image.save(ROOT/'previews'/f'{name}.png',optimize=True)

if __name__=='__main__':
    for name,frame in generate().items():
        outname={'sportster-14.4k':'main-14.4k','sportster-28.8k':'main-28.8k','sportster-33.6k':'main-33.6k','sportster-56k':'main-56k','phonebook':'phonebook-28.8k','settings':'settings-28.8k','hardware':'hardware-tests-28.8k'}[name]
        render(outname,frame)
        print(outname)
