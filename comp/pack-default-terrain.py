#!/usr/bin/env python3
"""Assemble original 16px terrain quarters into the existing 32px tile atlas.

Requires ImageMagick (magick). No resizing, recoloring or resampling is applied.
Non-terrain frames and their atlas coordinates are preserved.
"""
import json
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
# Top-left, top-right, bottom-left, bottom-right in the original 18-piece sheet.
QUARTERS = {
    3: (14,10,5,7), 4: (15,10,4,7), 5: (15,11,4,6),
    6: (14,10,16,12), 7: (15,10,17,12), 8: (15,11,17,13),
    9: (3,1,16,12), 10: (2,1,17,12), 11: (2,0,17,13),
    38: (2,1,4,7), 39: (3,1,5,7), 40: (2,0,4,6),
    100: (15,9,4,7), 101: (8,10,17,12),
    102: (15,9,17,12), 103: (8,10,4,7),
}

def main():
    source = ROOT / 'Raw Images/DF_DefaultTerrain/atlas.png'
    meta = json.loads(source.with_suffix('.json').read_text())
    if meta['meta']['size'] != {'w':96,'h':48}:
        raise ValueError('Expected the original compact 18-quarter atlas')
    sheet = ROOT / 'assets/Sheets/DF_Blocks-uhd.png'
    frames = plistlib.loads(sheet.with_suffix('.plist').read_bytes())['frames']
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        quarters = []
        for i in range(18):
            p = temp / f'q{i}.png'; quarters.append(p)
            subprocess.run(['magick',str(source),'-crop',f'16x16+{i%6*16}+{i//6*16}', '+repage',str(p)],check=True)
        command = ['magick',str(sheet),'-define','compose:outside-overlay=false']
        for tile, pieces in QUARTERS.items():
            entry = frames[f'{tile}.png']
            x,y,w,h = map(int,re.findall(r'-?\d+',entry['textureRect']))
            if (w,h)!=(32,32) or entry['textureRotated']:
                raise ValueError(f'Unsupported packed frame for tile {tile}')
            output = ROOT / f'Raw Images/DF_Blocks-uhd/{tile}.png'
            args = ['magick','-size','32x32','canvas:none']
            for piece,(dx,dy) in zip(pieces,((0,0),(16,0),(0,16),(16,16))):
                args += [str(quarters[piece]),'-geometry',f'+{dx}+{dy}','-composite']
            subprocess.run(args+[str(output)],check=True)
            # Src replaces old pixels including transparent edge cutouts.
            command += [str(output),'-geometry',f'+{x}+{y}','-compose','Src','-composite']
        result = temp/'sheet.png'
        subprocess.run(command+[str(result)],check=True)
        sheet.write_bytes(result.read_bytes())
    print('Updated 16 default terrain variants using all 18 original quarter sprites.')

if __name__ == '__main__': main()
