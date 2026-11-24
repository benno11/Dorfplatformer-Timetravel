# Manual channel updates

In GitHub Actions, select **Update prerelease** or **Update stable**, choose
**Run workflow**, and enter a published build tag such as `draft-main-429`.
Neither action runs automatically. Both reuse the selected release's binaries
and verify the uploaded manifests. They do not rebuild the game.

Update prerelease sets the current weekly snapshot and floating prerelease
manifests. Existing weekly automation remains active.

Update stable publishes floating stable manifests for the available platform
and edition packages, then commits the stable manifest to
`Benno111/Dorfplatformer-API`, preserving the other channel settings.
Before its first use, add the game repository Actions secret
`UPDATE_CHANNELS_TOKEN`: a token with **Contents: read and write** access to
both repositories. The ordinary `GITHUB_TOKEN` cannot write to the API repository.
The action fails before publishing if the secret is missing.

These changes add the workflows only; merging them does not promote a release.
