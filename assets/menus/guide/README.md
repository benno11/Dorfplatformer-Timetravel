# DF Slim Menu Guide

Menus are `.menu` text files in `assets/menus`. The title screen loads:

- `mainplay.menu` from the Play button.
- `mainedit.menu` from the Editor button.
- `mainsetting.menu` from the Settings button.

All script menus use `assets/Sheets/DF_Menus-uhd.png` and its `.plist` for window panels,
buttons, toggle checkboxes, and back/close icons. Toggle icons follow the setting value
automatically; no additional script commands are needed. The selected button is brighter
for keyboard navigation. If the atlas is unavailable, menus use plain panels and buttons.

Menu switches are immediate, with no fade animation. Script sources and level lists
are cached for the menu session, and script menus reuse the frontend's loaded atlas.
Online lists and moderator permissions load in the background. Use the generated
level browser's Refresh button to fetch updates; uploads, deletions, votes, ratings,
and editor changes invalidate the relevant cached list automatically. Returning to
the title screen ends the session, so reopening menus also reloads script changes.

Commands:

- `title "Text"` sets the menu title.
- `scale 0.8` scales the whole menu, including text, buttons, icons, and spacing (default `1`, range `0.25` to `4`).
- `menu_align right bottom` anchors the scaled panel in the screen (default `center center`).
- `align left top` aligns subsequent text/button objects within the content area on both axes (default `center center`).
- `text_align left center` aligns labels within subsequent objects on both axes (default `center center`).
- `text "Text"` draws static text. Variables use `${name}`.
- `button "Label" menu othermenu` opens another `.menu`.
- `button "Label" campaign` opens campaign level select.
- `button "Label" levels` opens the combined level select.
- `button "Label" custom` opens custom/editor levels.
- `button "Label" menu editlevels` opens the editor level browser menu.
- `button "Label" menu locallevels` opens the generated local-level pages.
- `button "Label" menu onlinelevels` opens the generated online-level pages.
- `button "Label" create_level` opens the local level editor for a new level.
- `button "Label" level_page local:next` goes to the next local-level page.
- `button "Label" level_page online:prev` goes to the previous online-level page.
- `button "Label" level_filter online:hard` filters online levels by difficulty.
- `button "Label" saved` starts the active save.
- `button "Label" save_slot 1` selects slot 1 and starts it; empty slots begin a new game.
- `button "Label" select_save 1` selects slot 1 without starting the game.
- `button "Label" delete_save 1` deletes slot 1.
- `button "Label" start assets/levels/level_001.txt` starts a specific level path.
- `button "Label" toggle menu_music_enabled` toggles a boolean setting.
- `button "Label" inc music_volume` or `dec music_volume` changes a numeric setting.
- `button "Label" get var=/api/path` performs a GET request and stores the response in `${var}`.
- `button "Label" back` returns to the title screen.
- `button "Label" quit` closes the game.
- `if var skip 2` skips the next 2 menu lines when `var` is boolean false.
- `if var false skip 2` is the same, written more explicitly.

Supported built-in variables include `${username}`, `${api}`, `${active_save_slot}`, `${save_slot_1_status}`, `${save_slot_2_status}`, `${save_slot_3_status}`, `${save_slot_1_exists}`, `${save_slot_2_exists}`, and `${save_slot_3_exists}`. Data fetched with `get` is saved as a variable for the current menu session.

## Scrolling, scale, and alignment

Menus automatically scroll vertically when their content exceeds the visible area.
Use the mouse wheel, drag the scrollbar, or swipe up/down on a touchscreen. Arrow
keys select buttons and scroll them into view. Page Up/Down move through buttons
by a page; Home/End select the first/last button. In menus containing only text,
these keys scroll the content instead. The title stays fixed while content scrolls.
Buttons clipped outside the content area cannot be clicked.

Horizontal alignment accepts `left`, `center`, and `right`. Vertical alignment
accepts `top`, `center`, and `bottom`; `left`/`right` are also accepted as aliases
for `top`/`bottom`. Alignment directives affect subsequent objects, so place an
`align` line before an individual object to give it its own alignment.

Objects with the same vertical alignment form a stack in script order. Top stacks
start at the top, bottom stacks end at the bottom, and center stacks occupy the
middle space between them. Stacks avoid overlapping; overflowing stacks become
one scrollable area. Text and buttons participate in the same layout.

`menu_align` positions the entire scaled panel. Scales above `1` enlarge its
contents while keeping the panel inside the screen and making overflow scrollable.
`text_align` controls a label inside its object, independently of the object's
position. Existing scripts default to scale `1` and centered alignment.

```menu
title "Layout Example"
scale 0.8
menu_align center center

align left top
text "This text sits at the top left."

align center center
text_align left center
button "Display Settings" menu settingsdisplay
button "Audio Settings" menu settingsaudio

align right bottom
text_align center center
button "Back" back
```

Save slot status values:

- `ACTIVE`: the slot is currently selected.
- `READY`: the slot has save data.
- `EMPTY`: the slot has no save data.

Generated local and online level menus:

- `editlevels.menu` is the editor-side menu that links to local and online browsing.
- `locallevels` and `onlinelevels` are generated by the engine.
- The generated local-level page includes a Create button that opens the editor.
- Selecting a generated level opens a dedicated detail page before launching it.
- Each generated page shows up to 10 levels.
- Page controls are `level_page local:prev`, `level_page local:next`, `level_page online:prev`, and `level_page online:next`.
- Filter controls are `level_filter local:all`, `level_filter local:unrated`, `level_filter online:all`, `level_filter online:easy`, `level_filter online:medium`, `level_filter online:hard`, and `level_filter online:unrated`.
- Difficulty filters mean easy `1-3`, medium `4-6`, hard `7-9`, and unrated `0` or missing difficulty.
- Local levels usually have no server difficulty, so they appear under `all` and `unrated`.

Level detail pages:

- Local level details show name, difficulty, verification state, and actions for Play, Edit, Upload, Delete, and Back.
- Online level details show name, difficulty, and actions for Play, Update Download, and Back.
- Moderator accounts also see a Rate button on online level details. It cycles difficulty from unrated to `1`, then up through `9`, then back to unrated.
- `Update Download` refreshes the downloaded/cached copy of that online level from the server.
- Uploading a local level requires verification.

Level verification:

- A local level becomes verified when it is beaten.
- Editing and saving a local level clears verification.
- Verification is stored beside the level as `<level file>.verify.json`.
- The upload command refuses unverified local levels.

## Get Command

`get` downloads text from a URL and stores the response in a menu variable.

Button `get` requests run in the background while the menu remains usable. A loading
indicator appears until the response is applied on the main thread. Repeated requests
for the same variable are ignored while one is pending. Leaving the menu session
discards its results safely. Top-level `get` waits with a loading screen so subsequent
`set` and `if` lines can use the downloaded value in script order.

There are two ways to use it:

```menu
get variable_name /api/path
button "Load Data" get variable_name=/api/path
```

The top-level `get` runs when the menu is loaded. The button form runs only when the button is pressed.

The variable name goes on the left side. After the request finishes, the response can be shown with `${variable_name}`:

```menu
get server_status /api/status
text "Server: ${server_status}"
```

Button example:

```menu
button "Refresh Status" get server_status=/api/status
text "Server: ${server_status}"
```

Relative URLs use the configured game server URL. If the configured game server is `https://example.com`, then `/api/status` becomes `https://example.com/api/status`.

Absolute URLs are used as-is:

```menu
get news https://example.com/news.txt
```

Variables can be used inside the request path:

```menu
get player_info /api/player/${username}
text "${player_info}"
```

If the game server URL is empty, the HTTP request fails, or curl/HTTP support is unavailable in the build, the variable is saved as an empty string. The raw response body is stored; JSON is not parsed automatically.

Use `if` after `get` to hide lines when a response is empty or false-like:

```menu
get show_bonus /api/menus/show-bonus
if show_bonus skip 1
button "Bonus" menu bonus
```

## Default API

The default game API is configured in `assets/config.json`:

```text
http://critique-unsmooth.tun.ply.gg:49642/
```

Slim menu `get` uses this value when the request path is relative. For example:

```menu
get health /health
```

requests:

```text
http://critique-unsmooth.tun.ply.gg:49642/health
```

Useful default GET endpoints for menus:

- `/health` returns server health, for example `{"status":"ok","api_version":1}`.
- `/api.json` returns the service descriptor and API version.
- `/levels.json?shallow=true` returns an object of level IDs mapped to `true`.
- `/levels.json?metadata=true` returns shared level metadata.
- `/levels/<id>.json` returns one full shared level object.
- `/levels/<id>/data.json` returns one shared level's raw level data as a JSON string.
- `/update-manifest.json` returns updater metadata when the server operator has published it.

Examples:

```menu
get api_info /api.json
text "${api_info}"

get level_ids /levels.json?shallow=true
text "${level_ids}"

get first_level /levels/example-level/data.json
text "${first_level}"
```

Account and upload endpoints exist on the same API, but slim-menu `get` should not be used for them because they need POST, PUT, DELETE, JSON bodies, or auth headers:

- `/api/auth/register`
- `/api/auth/login`
- `/api/auth/lookup`
- `/api/auth/update`
- `/api/auth/logout`
- `PUT /levels/<id>.json`
- `DELETE /levels/<id>.json`
- `PUT /levels/<id>/difficulty`

The generated online level detail page uses `/api/auth/lookup` to decide whether the signed-in account is a moderator. The Rate button is hidden unless the lookup response contains a moderator/admin signal such as `mod`, `moderator`, `isMod`, `isModerator`, `admin`, `isAdmin`, a `role` of `mod`, `moderator`, `admin`, or `owner`, or the same values inside `roles`, `claims`, or `customClaims`.

When a moderator presses Rate, the client sends:

```json
{ "difficulty": 1 }
```

to `PUT /levels/<id>/difficulty` with the account bearer token. Values are `0-9`; `0` clears the rating.

Example:

```menu
set show_extra false
if show_extra skip 1
button "Extra Button" back
```
