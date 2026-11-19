# iOS sleep and SideStore

The iOS event loop saves private settings, pauses mixer tracks without discarding
stream positions, and blocks on foreground events instead of drawing background
frames. Gameplay stays paused on return; unsaved editor changes are autosaved.
The app does not request background audio or other persistent background modes.
iOS can still reclaim a suspended app under memory pressure.

## Quick resume

iOS keeps a separate `quick_resume.cbor` in the edition's save directory. A live
snapshot is written every five seconds and when backgrounding. On cold launch,
a compatible snapshot skips the menu and intro and restores gameplay paused.
Player, map, coins, checkpoints, moving platforms and boss state are retained.
Normal progression slots and encrypted private saves keep their existing formats.
Changed or missing level files and invalid snapshots fall back to normal startup.
Leaving a level, dying, or completing it invalidates the old live snapshot.
Remote URL levels do not use quick-resume snapshots.

During startup and foreground recovery, iOS shows a silent loading frame until
the complete UI/game frame is presented. Protected private saves are retried
after unlock; existing private data must load successfully before it can be
replaced. Temporary key/file failures do not permanently disable the store.

## SideStore source

Source URL: https://benno11.github.io/Dorfplatformer-Timetravel/source.json
Landing page: https://benno11.github.io/Dorfplatformer-Timetravel/

In repository Settings → Pages, select **GitHub Actions** as the source once.
Then run the **Publish SideStore source** workflow. It also refreshes after Build
Game completes and every six hours. No additional repository secrets are needed.

The publisher selects the latest main-branch push/manual run whose iOS device
job succeeded, verifies the IPA against its build manifest, and publishes it at
a per-build public `sidestore-build-N` release URL. Draft build downloads are not
used in the public feed. Simulator packages are excluded. The JSON includes the
actual bundle identifier, version, build number, iOS minimum, size, and SHA-256.
The app remains unsigned unless CI signing is configured; SideStore re-signs it.

Run local source tests with:

    python3 -m unittest discover -s comp -p test_sidestore.py

### Update channels

- Prerelease: https://benno11.github.io/Dorfplatformer-Timetravel/prerelease/source.json
- Nightly: https://benno11.github.io/Dorfplatformer-Timetravel/nightly/source.json

The original `/source.json` remains a nightly alias for existing subscribers.
The landing page offers both channels. They share the actual IPA bundle identifier;
subscribe to your preferred channel to avoid competing updates for the same app.

Nightly selects the newest supported successful main-branch iOS device build.
Prerelease pins the newest successful device build available at Tuesday 00:00 UTC,
starting from November 10, 2026. Its choice is stored in the immutable weekly
`sidestore-prerelease-YYYY-MM-DD` release. When initializing a week with no supported
build before its cutoff, the oldest available supported build initializes the pin.
Builds before 417 are excluded because they lacked working iOS media decoders.
These iOS pins are independent of desktop/Android prerelease selections, which
may not include an installable iOS binary.
