#!/usr/bin/env python3
"""Match original slope fills to the default terrain; preserve contours and alpha."""
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def rgba(path):
    return bytearray(subprocess.check_output(['magick', str(path), '-depth', '8', 'rgba:-']))

def main():
    source = rgba(ROOT / 'assets/Sheets/DF_DefaultTerrain.png')
    def quarter(piece, x, y):
        offset = ((piece // 6 * 16 + y) * 96 + piece % 6 * 16 + x) * 4
        return source[offset:offset+4]
    soil = [quarter((15,10,17,12)[y//16*2+x//16], x%16, y%16) for y in range(32) for x in range(32)]
    grass = [quarter((2,1)[x//16], x%16, y) for y in range(16) for x in range(32)]
    sheet = ROOT / 'assets/Sheets/DF_Blocks-uhd.png'
    frames = plistlib.loads(sheet.with_suffix('.plist').read_bytes())['frames']
    with tempfile.TemporaryDirectory() as directory:
        command = ['magick', str(sheet), '-define', 'compose:outside-overlay=false']
        for tile in range(83,88):
            path = ROOT / f'Raw Images/DF_Blocks-uhd/{tile}.png'
            pixels = rgba(path)
            before = bytes(pixels)
            for x in range(32):
                green_rows = [y for y in range(32) if pixels[(y*32+x)*4+3] and pixels[(y*32+x)*4+1] > max(pixels[(y*32+x)*4], pixels[(y*32+x)*4+2])]
                top = min(green_rows, default=0)
                for y in range(32):
                    offset = (y*32+x)*4
                    r,g,b,a = pixels[offset:offset+4]
                    if not a: continue
                    if r > g and r > b:
                        pixels[offset:offset+3] = soil[y*32+x][:3]
                    elif g > r and g > b:
                        row = min(y-top,15)
                        while row > 0 and not (grass[row*32+x][1] > max(grass[row*32+x][0],grass[row*32+x][2])): row -= 1
                        pixels[offset:offset+3] = grass[row*32+x][:3]
            assert pixels[3::4] == before[3::4], 'Slope silhouettes changed'
            for offset in range(0,len(pixels),4):
                if before[offset:offset+3] == b'\0\0\0':
                    assert pixels[offset:offset+3] == before[offset:offset+3], 'Slope outlines changed'
            subprocess.run(['magick','-size','32x32','-depth','8','rgba:-',str(path)], input=pixels, check=True)
            entry = frames[f'{tile}.png']
            x,y,w,h = map(int,re.findall(r'-?\d+',entry['textureRect']))
            assert (w,h)==(32,32) and not entry['textureRotated']
            command += [str(path),'-geometry',f'+{x}+{y}','-compose','Src','-composite']
        output = Path(directory)/'sheet.png'
        subprocess.run(command+[str(output)],check=True)
        sheet.write_bytes(output.read_bytes())
    print('Matched slopes 83–87; original outlines and transparency preserved.')

if __name__ == '__main__': main()
