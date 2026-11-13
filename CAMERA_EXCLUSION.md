# Camera exclusion object

Place **CAMERA EXCLUSION (99)** from the editor's object palette. Its pink `CAM`
marker covers one block centered on the object. Use the brush or neighboring
placements to keep a larger area offscreen. The normal object format preserves
its position and layer; it needs no parameter payload or new level version.

During gameplay the marker is invisible, has no player collision or water
interaction, and pushes the camera left, right, up, or down to a nearby clear
view. It applies after camera locks, map-edge clamping and pixel snapping, using
the actual visible world size. Multiple objects are checked together. Exclusion
can move the camera beyond level bounds when necessary, and can move the player
offscreen if the player is inside or too close to an excluded area.

Wrapped copies also count. A fully wrapped map smaller than the viewport can
make exclusion mathematically impossible; in that case the camera stays stable
rather than searching forever or drifting. Design wrapped levels with room for
a clear view.
