# DF Slim Menu Guide

Menus are `.menu` text files in `assets/menus`. The title screen loads:

- `mainplay.menu` from the Play button.
- `mainedit.menu` from the Editor button.
- `mainsetting.menu` from the Settings button.

Commands:

- `title "Text"` sets the menu title.
- `text "Text"` draws static text. Variables use `${name}`.
- `button "Label" menu othermenu` opens another `.menu`.
- `button "Label" campaign` opens campaign level select.
- `button "Label" levels` opens the combined level select.
- `button "Label" custom` opens custom/editor levels.
- `button "Label" saved` starts the active save.
- `button "Label" start assets/levels/level_001.txt` starts a specific level path.
- `button "Label" toggle menu_music_enabled` toggles a boolean setting.
- `button "Label" inc music_volume` or `dec music_volume` changes a numeric setting.
- `button "Label" get var=/api/path` performs a GET against the configured game server and stores the response in `${var}`.
- `button "Label" back` returns to the title screen.
- `button "Label" quit` closes the game.

Supported built-in variables include `${username}` and `${api}`. Data fetched with `get` is saved as a variable for the current menu session.
