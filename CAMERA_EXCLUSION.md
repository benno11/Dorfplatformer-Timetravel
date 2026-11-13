# Camera exclusion object

Place **CAMERA EXCLUSION (99)** from the editor's object palette. Its pink `CAM`
marker covers one block centered on the object. Use the brush or neighboring
placements to keep a larger area offscreen. The normal object format preserves
its position and layer; it needs no parameter payload or new level version.

During gameplay the marker is invisible, has no player collision or water
interaction, and keeps the camera on its current safe side while smoothly
following the target. Movement into an edge is blocked; movement along the edge
continues. Swept checks prevent fast movement from skipping across an exclusion.
Adjacent and overlapping objects are checked together, so painted seams do not
stop sliding and object ordering does not change the movement. Once an edge ends,
the camera eases back toward its target.

Exclusion applies after camera locks and map-edge clamping, using the actual
visible world size. Normal levels without exclusions keep their existing camera
behavior. Level reloads reset the sliding state; initial placement or viewport
resizing can require an immediate correction to obtain a clear view. Exclusion
can move the camera beyond level bounds when necessary, and can move the player
offscreen if the player is inside or too close to an excluded area.

Wrapped copies also count. A fully wrapped map smaller than the viewport can
make exclusion mathematically impossible; in that case the camera stays stable
rather than searching forever or drifting. Design wrapped levels with room for
a clear view.
