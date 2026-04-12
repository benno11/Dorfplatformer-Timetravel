Platformer Engine

Build locally:
- Windows PowerShell: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1`
- Windows PowerShell + run after build: `powershell -ExecutionPolicy Bypass -File .\build-local.ps1 -Run`
- Linux/macOS: `./build-local.sh`
- Linux/macOS + run after build: `./build-local.sh --run`

Manual CMake (separate folder):
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

Level upload API (GitHub Pages):
- Static client lives in `pages/level-api/` and uploads to Firebase RTDB REST.
- Deployment workflow: `.github/workflows/level-api-pages.yml`.
- After Pages is enabled for the repo (GitHub Actions source), the app URL is:
  - `https://<your-user>.github.io/<your-repo>/`
- API descriptor JSON:
  - `https://<your-user>.github.io/<your-repo>/api.json`
- Upload format written by the app:
  - `PUT <level_server_url>/levels/<id>.json`
  - Body includes `data` (raw level text) plus metadata fields (`name`, `owner`, `level_id`, timestamps).

Custom levels:
- Main menu `Play` opens campaign levels
- Main menu `Editor` opens custom levels
- Desktop: put `.txt` or `.bnnlvl` files in `custom_levels/` or `assets/custom_levels/`
- Android/assets-based: provide `assets/custom_levels/levels.json` with a `levels` array
- Realtime DB: set `level_server_url` in `assets/config.json` (and Android asset config) to your Firebase RTDB base URL.
  - For authenticated writes: `level_server_auth_token`
  - Account username for uploads: `level_server_account_username`
  - Supported remote list endpoints:
    - Firebase Realtime Database REST API is used (`.json` endpoints, `shallow=true` for ID listing).
    - `<level_server_url>/levels.json` (object keys are IDs; each level read from `/levels/<id>/data.json`)
    - `<level_server_url>/custom_levels/levels.json` with `{ "levels": ["level_a.txt", ...] }`
    - `<level_server_url>/custom_levels.json` with `{ "levels": ["https://.../file.txt", ...] }`
  - Remote user levels are downloaded and cached into the app save folder under `user_levels/`.
  - Upload IDs are now written as `username-levelname`.

Firebase Realtime Database rules:
- Use `firebase-realtime-database.rules.json` for RTDB rules.
- These rules require authenticated writes and enforce upload payload shape for `/levels/<username-levelname>`.
