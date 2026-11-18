# iOS sleep and SideStore

The iOS event loop saves private settings, pauses mixer tracks without discarding
stream positions, and blocks on foreground events instead of drawing background
frames. Gameplay stays paused on return; unsaved editor changes are autosaved.
The app does not request background audio or other persistent background modes.
iOS can still reclaim a suspended app under memory pressure.

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
