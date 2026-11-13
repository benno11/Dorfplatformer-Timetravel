# Camera exclusion object

Place **CAMERA EXCLUSION (99)** from the editor's object palette. Its pink `CAM`
marker covers one block centered on the object. Use the brush or neighboring
placements to keep a larger area offscreen. The normal object format preserves
its position and layer; it needs no parameter payload or new level version.

During gameplay the marker is invisible, has no player collision or water
interaction, and keeps the camera on its current safe side while following the
player immediately, with no follow easing. Movement into an edge is blocked; movement along the edge
continues. Swept checks prevent fast movement from skipping across an exclusion.
Adjacent and overlapping objects are checked together, so painted seams do not
stop sliding and object ordering does not change the movement. Once an edge ends,
the camera can follow the unblocked target immediately.

Place **DIAGONAL CAMERA EXCLUSION (100)** for a triangular half-block instead of
a square. Its orange `DIA` marker shows the filled/excluded half. Configure its
rotation with the editor's Configure Object action: `0` bottom-right, `1`
bottom-left, `2` top-left, `3` top-right. The rotation is saved using the existing
object-parameter payload. Adjacent triangles can form longer diagonal edges.
The camera slides along the actual diagonal, not its rectangular bounding box,
and diagonal objects can be combined with square exclusions.

Exclusion applies after camera locks and map-edge clamping, using the actual
visible world size. Normal levels without exclusions keep their existing camera
behavior. Level reloads reset the sliding state; initial placement or viewport
resizing can require an immediate correction to obtain a clear view. Exclusion
can move the camera beyond level bounds when necessary, and can move the player
offscreen if the player is inside or too close to an excluded area.

Wrapping/clamping changes immediately when wrap is enabled or disabled. The
exclusion controller revalidates against the new visible copies instead of
retaining the previous wrap image or blending outside a non-wrapping level.
Boss sprites, their debug hitboxes, dropped coins, and boss fireball/explosion
effects draw their visible wrapped copies too, so a shifted camera image cannot
hide those actors.

Wrapped copies also count. A fully wrapped map smaller than the viewport can
make exclusion mathematically impossible; in that case the camera stays stable
rather than searching forever or drifting. Design wrapped levels with room for
a clear view.
