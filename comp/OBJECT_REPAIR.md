Bundled level object repair
==========================

`python3 comp/repair-level-objects.py` checks every modern bundled level for
out-of-bounds or off-grid object positions. `--fix` restores invalid positions
from `assets/levels/object-reference.json`. Gameplay and the editor also apply
this repair when loading modern bundled campaign levels, without modifying
source files at runtime. Build jobs repair their checkout before packaging.

Repairs require an exact match of object count and ordered IDs against the
reference. Valid edited coordinates are preserved. Fractional parameter-object
coordinates are supported. Unknown/mismatched lists are reported by the build
validator rather than guessed; custom levels are never repaired by the loader.

After intentionally changing object lists or positions, validate the changes and
run `python3 comp/repair-level-objects.py --record-reference`. Commit the changed
levels and reference together. Refresh is refused while invalid positions remain.
The reference is position data only; tile maps and object properties stay intact.
