Platformer Engine

Build (separate folder):
- Configure: `cmake -S . -B .build`
- Build: `cmake --build .build --config Release`

Microsoft Store update helper (Windows):
- Run: `powershell -ExecutionPolicy Bypass -File build/force-msstore-update.ps1`
- Note: this script uses `winget` to refresh sources and apply available Microsoft Store package updates.

Features included:
- sdl3 C++ 2D platformer
- sdl3 version required: 3.4.0 or newer
- Background + solid tile grids
- Tilesheet-based BG rendering
- Object system (ID-based, JSON-defined)
- Level format: .bnnlvl
- Built-in levels in assets/levels
- Level select menu (campaign/custom)
- Mobile touch controls + editor UI

Custom levels:
- Main menu `Play` opens campaign levels
- Main menu `Editor` opens custom levels
- Desktop: put `.txt` or `.bnnlvl` files in `custom_levels/` or `assets/custom_levels/`
- Android/assets-based: provide `assets/custom_levels/levels.json` with a `levels` array
- Realtime DB: set `level_server_url` in `assets/config.json` (and Android asset config) to your Firebase RTDB base URL.
  - Optional for protected DB rules: `level_server_auth_token`
  - Supported remote list endpoints:
    - Firebase Realtime Database REST API is used (`.json` endpoints, `shallow=true` for ID listing).
    - `<level_server_url>/levels.json` (object keys are IDs; each level read from `/levels/<id>/data.json`)
    - `<level_server_url>/custom_levels/levels.json` with `{ "levels": ["level_a.txt", ...] }`
    - `<level_server_url>/custom_levels.json` with `{ "levels": ["https://.../file.txt", ...] }`
  - Remote user levels are downloaded and cached into the app save folder under `user_levels/`.
