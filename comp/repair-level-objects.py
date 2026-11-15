#!/usr/bin/env python3
"""Check bundled object coordinates; --fix restores invalid slots from the reference."""
import argparse
import json
import math
from pathlib import Path
import re


def position_bad(identity, x, y, width, height):
    if not (math.isfinite(x) and math.isfinite(y) and 0 <= x < width and 0 <= y < height):
        return True
    return identity < 1000 and (abs(math.remainder(x-16, 32)) > .01 or abs(math.remainder(y-16, 32)) > .01)


def repair_text(text, reference, fix=False):
    lines = text.splitlines(keepends=True)
    header = re.search(r'DFLVL[234]\s+(\d+)\s+(\d+)', text)
    section = next((i for i, line in enumerate(lines) if re.fullmatch(r'OBJ \d+\s*', line)), None)
    if not header or section is None:
        return text, ['Unsupported or malformed level format']
    width, height = (int(v)*32 for v in header.groups())
    count = int(lines[section].split()[1])
    try:
        rows = [list(map(float, lines[section+1+i].split())) for i in range(count)]
        if any(len(row) != 3 or not row[0].is_integer() for row in rows):
            raise ValueError('bad object row')
    except (ValueError, IndexError):
        return text, ['Malformed object section']
    matches = len(reference) == count and all(int(row[0]) == ref[0] for row, ref in zip(rows, reference))
    issues = []
    for i, (identity, x, y) in enumerate(rows):
        bad = position_bad(identity, x, y, width, height)
        if not bad:
            continue
        if fix and matches:
            _, rx, ry = reference[i]
            if position_bad(identity, rx, ry, width, height):
                issues.append(f'Object {i}: invalid reference')
                continue
            lines[section+1+i] = f'{int(identity)} {rx:g} {ry:g}\n'
        else:
            issues.append(f'Object {i} ({int(identity)}): invalid position ({x:g}, {y:g}); ' +
                          ('no matching reference' if not matches else 'run with --fix'))
    return ''.join(lines), issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fix', action='store_true')
    parser.add_argument('--record-reference', action='store_true', help='Record intentional edits after all levels validate')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    references = json.loads((root/'assets/levels/object-reference.json').read_text())
    errors = []
    clean = {}
    for path in sorted((root/'assets/levels').glob('*.txt')):
        name = path.name
        original = path.read_text()
        if not re.search(r'DFLVL[234]\s+\d+\s+\d+', original):
            continue
        reference = references.get(name, [])
        result, issues = repair_text(original, reference, args.fix)
        if result != original:
            path.write_text(result)
            print(f'Repaired {name}')
        errors.extend(f'{name}: {issue}' for issue in issues)
        if not issues:
            section = re.search(r'^OBJ (\d+)\s*$', result, re.M)
            clean[name] = [[int(row[0]), float(row[1]), float(row[2])] for row in
                           (line.split() for line in result[section.end():].strip().splitlines()[:int(section[1])])]

    for error in errors:
        print(error)
    if errors:
        return 1
    if args.record_reference:
        (root/'assets/levels/object-reference.json').write_text(json.dumps(clean, separators=(',', ':'))+'\n')
        print('Recorded validated object reference')
    print(f'Validated {len(clean)} bundled levels')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
