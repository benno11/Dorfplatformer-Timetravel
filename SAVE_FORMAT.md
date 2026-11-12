# Save format

New save-slot files contain exactly **four binary bytes**. Format number is `3`.
There is no text header, separator, or snapshot payload. Existing slot filenames
are retained for migration. Achievements remain in the global client config.

| Byte | Low bits / nibble | High bits / nibble |
| --- | --- | --- |
| 0 | Format number `3` (whole byte) | |
| 1 | World 1 buttons: bits 0–1; world 2: bits 2–3 | World 3: bits 4–5; world 4: bits 6–7 |
| 2 | World 5 buttons: bits 0–1; world 6: bits 2–3 | World ID |
| 3 | Hardmode / World Select flags | Area ID |

Each world's first bit represents its area-1 button and its second bit its
area-2 button. Alternative buttons in an area share the same flag.

The flags nibble uses bit 0 for Hardmode unlocked, bit 1 for World Select
unlocked, bit 2 for Hardmode completed, and bit 3 for an active Hardmode run.
Campaign world IDs are 1–7, with area IDs 1–3 (world 7 has only area 1).
Hardmode uses world groups 1–2 and area IDs 1–10 for its twenty levels.

Continue restarts the saved campaign area in its initial time period: period 1
for areas 1–2 in worlds 1–5; period 3 for area 3 and worlds 6–7. Saved button
flags are restored. Hardmode continues at the saved level's start; a death saves
level 1. Historical best Hardmode level has no dedicated storage: it is derived
from the saved run or completion flag.

Example hex: `03 9B 3D 23` means format 3, world 3, area 2, both unlocks enabled,
and button masks `[3, 2, 1, 2, 1, 3]` for worlds 1–6.

JSON saves and the previous header-based versions 1 (17 bytes) and 2 (12 bytes)
remain readable and upgrade on the next successful save. Unknown versions,
invalid world/area IDs, truncation, and trailing payloads are rejected. Rejected
loads cannot overwrite the original save.
