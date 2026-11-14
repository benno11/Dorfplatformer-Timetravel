# Update channels

Open Settings → Updater and choose **Use stable**, **Use prerelease**, or
**Use nightly**, then check for updates. The selection is saved separately in
the edition's user preferences and survives installation updates.
`update_channel` in `assets/config.json` sets the default for new users. The game, sandbox updater and
Windows tray resolve the same setting. `stable` remains the default and uses
`update_manifest_url` (or the legacy `windows_update_manifest_url`). Unknown
channels do not fall back to a different channel.

Nightly selects the newest successfully packaged build from `main` or `nightly`
for the current operating system, architecture and full/lite edition. Each
platform job publishes independently, so a failed Windows build does not block
an Android update. Pull requests and agent branches cannot publish channels.
Android uses a universal APK; iOS uses the device IPA, never a simulator package.
Platforms with no build have no channel download rather than another OS's file.

Prerelease is a fixed weekly snapshot. The anchor is **2026-11-10 00:00 UTC**
(01:00 in Berlin), recurring every Tuesday. The promoter chooses the latest
successful build completed at or before that week's cutoff for each target.
The scheduled workflow can run late without changing the cutoff. Repeating it
reuses the stored snapshot. With no qualifying build, the previous pin remains;
there is no fabricated historical November 10 build. After enabling this
workflow, the first new snapshot can only include builds actually published
before its cutoff. Run “Weekly prerelease snapshot” manually to reconcile the
current week after enabling it.

Packages live at build-specific release URLs. Each channel exposes a separate
`update-manifest.json` release asset, including its build ID, commit and package
URL. Weekly dated snapshot releases retain the selected manifest. Do not delete
build releases referenced by snapshots. Git branches hold source; these release
manifests select the packages offered by the updater.

CI stamps the edition into the packaged config. Builds from `nightly` and
`prerelease` default to their respective channel. Existing downgrade protection remains: selecting
an older prerelease after a newer nightly does not install the older bundle.
The Linux/Android/iOS updater opens the package download using the existing OS
flow; Windows uses its existing installer handoff.

## Checks

```sh
python3 -m unittest discover -s comp -p test_update_channels.py
c++ -std=c++17 -Isrc -Ithird_party tests/UpdateManifestTest.cpp -o /tmp/update_manifest_test
/tmp/update_manifest_test
```
