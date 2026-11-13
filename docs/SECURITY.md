# Private profile and process protection

Achievements and account username/session token now live in `savedata_secret`
under the edition's normal save directory, shared across campaign slots. The
four-byte campaign save format is unchanged. Passwords are not stored.

## Encrypted profile

The v1 envelope is `DFSEC001` (8 bytes), random XChaCha20 nonce (24), Poly1305
tag (16), encrypted JSON, and an Ed25519 signature (64). The signature covers
all preceding envelope bytes. Verification uses a key derived from the device
master, never a public key supplied by the file. Independent encryption and
signing keys use keyed BLAKE2b with edition-specific domain separation. The
edition context is also AEAD associated data. The implementation uses vendored
Monocypher 4.0.2, including its standard Ed25519 module.

A random master key is kept separately:

- Windows: DPAPI-wrapped in `.keys/savedata_secret.key`.
- Android: wrapped with a non-exportable AES-GCM Android Keystore key, in that file.
- macOS/iOS: a device-only Keychain item.
- Linux: a 0600 key file in a 0700 `.keys` directory. This protects a copied save
  file or access by another local user; it does not protect a stolen complete
  user profile or access as the same user.

Saving first writes the authenticated profile, then removes legacy achievement
and credential fields from public client settings (including an existing local
fallback). Existing malformed, tampered, or unrecognized private saves are not
overwritten; a missing key for an existing save is not silently replaced. A
warning explains the problem. Credentials are bound to the server URL stored
inside the encrypted profile; editing the public URL cannot redirect a stored
token. Losing the device key makes the private save unreadable. Normal updates
keep the key; copying only `savedata_secret` to another device is insufficient.

This is local at-rest protection, not server-authoritative anti-cheat. Native
mods share the game's memory and account access. Someone controlling the game
or its OS account can obtain its data; valid old files can also be replayed.

## Process sandbox

On Windows, `platformer.exe` is the broker, not the game. It creates an
AppContainer process of itself and only that restricted child loads
`platformer.dll`. The broker grants that container read access to the installation,
write access to the edition's save folder and editable levels, and network
capabilities for online play. A one-process job prevents spawning child programs
and kills the game if the broker closes. The broker does not load the game DLL
or mods. Both matching files are required in Windows packages. Installation
and save folders must be separate dedicated folders; root/home directories are
rejected. Windows also rejects reparse points in those folders before granting
access, so a mod-created junction cannot expand the next launch's permissions.

Linux installs Landlock ABI 3 filesystem restrictions before starting SDL:
read-only installation/system runtime data, writable app saves and editable
levels, and existing legacy config files for migration. It does not grant access
to arbitrary home files. Seccomp blocks new programs/processes, cross-process
memory operations, tracing, external signalling and selected privileged kernel
interfaces while permitting game threads. After SDL connects to display/audio,
an additional filter prevents new Unix-socket/session-bus connections. Restart
Game creates a fresh child process so SDL can reconnect safely.

macOS applies a filesystem Seatbelt profile and prohibits spawning programs;
mobile platforms retain their OS application sandbox and scoped document picker.
A desktop without a working filesystem sandbox warns and keeps native mods
disabled. Windows refuses to launch an unprotected fallback.

Network access, display/audio handles and game-owned data remain available to
mods. X11 and other desktop services are not a complete isolation boundary;
this is defense in depth, not a VM or a safe environment for hostile native code.
External-program features, including launching an installer/browser from within
the game, may be blocked; use the external launcher for updates.

## Software pointer API and fail-fast behavior

The executable provides `DFSandboxAPI` (`src/SandboxPointerAPI.h`) to the game
DLL. It owns a process-local pointer table. The game holds opaque handles,
containing an ID and a keyed BLAKE2b authentication tag, instead of raw targets
at this boundary. The tag binds identity, backing address, bounds, type and
immutable contents. Every managed access validates the tag before reading or
writing; every managed jump validates the target type and tag before calling.
IDs are never reused within a process. Invalid, stale, forged, out-of-bounds or
read-only-write handles terminate immediately.

The game entry point and all native mod load/gameplay/menu/unload callbacks
use this API. Callback targets and userdata are copied into authenticated,
immutable records in the executable and are not retained as jump targets in
the game's loader. Buffer allocation/read/write/immutable-storage/release APIs
are also available for future engine migrations. They never return the backing
buffer address. The C ABI is versioned and has no STL objects across DLLs.

This does **not** rewrite every C++/SDL/STL pointer. The table resides in the
restricted game process's executable module, not in a separate address space;
it detects accidental corruption, but hostile native code in that process can
attack the runtime itself. OS process isolation is the boundary protecting the
rest of the device. Compiler checks complement the software handles: Windows
CFG (and MSVC XFG/CET where available), stack canaries, supported x86 CET and
AArch64 PAC-return/BTI, and Clang cross-DSO indirect-call CFI where the complete
toolchain supports it. Legacy uninstrumented mod DSOs remain compatible, with
weaker compiler CFI coverage. Return PAC/BTI is not universal pointer signing.

Fatal access violations, heap/stack corruption, failed handle authentication,
unhandled exceptions and allocation failure stop the process immediately.
POSIX signal handlers only write a mobile crash marker and call `_exit`; they
do not allocate, log, save, wait, or call UI code. A healthy desktop broker
shows the notification after the game exits. Mobile shows a notice on the next
launch because the crashed app process cannot safely show one itself. These
checks cannot detect every memory bug or guarantee its detection before damage.

## Windows build-pair signature

Both PE files contain a dedicated read-only `.dfpair` section, with a fixed
256-byte record: `DFPAIR01` magic (8), little-endian format version (4), edition
(4), random build ID (32), launcher BLAKE2b-512 digest (64), game DLL digest
(64), Ed25519 signature (64), and reserved zero bytes (16). The signature covers
the first 176 bytes, including both hashes and the build ID. Hashes cover the
complete files, normalizing only the 256-byte signature slot and the four-byte
PE checksum to zero. Section headers, imports, executable code, resources,
padding, and any appended data are included. The checksum is recalculated
after sealing and is not executable data.

The build tool generates a private Ed25519 seed in
`<build>/build-pair/signing.key` and a generated public-key header for the
launcher. Neither the seed nor the signing tool is included in packages. Keep
the seed private; do not upload the entire build directory as a public artifact.
Each changed binary pair receives a fresh random ID. An already valid pair is
left unchanged on incremental builds. Building `platformer` signs after linking;
the normal build and Windows packaging also depend on `platformer-seal`, so an
EXE-only relink is sealed before shipping. Sealing must run after any binary
rewriting/optimization. A later Authenticode signing or other file rewrite would
invalidate this format and requires explicit integration before enabling it.

Before `LoadLibraryExW` can execute any game DLL initializers, the restricted
launcher locks both files against write/delete sharing, validates their PE
sections, compares both signed records (and the loaded EXE's record), verifies
the signature using its embedded public key, and recomputes both file hashes.
Wrong builds, editions, architectures, missing signatures, and modified files
are rejected with an error. The DLL file remains locked until it is unloaded,
preventing ordinary modification or replacement between verification and load.

The launcher is the trust anchor. This detects a replaced or damaged DLL when
the launcher is trusted; it cannot stop someone who replaces/patches the launcher
and its checks too. The per-build key is not a publisher certificate or remote
attestation. Only the two game binaries are covered, not every dependency,
native mod, asset, or changes made to process memory after verification.

## Sandbox version and updater

`src/SandboxVersion.h` is the independent sandbox component version. About and
the updater menu display it in both editions; the About row copies it to the
clipboard. It is separate from the game release number and pointer API version.

Update checking now belongs to the sandbox runtime. On Windows, the EXE owns
the endpoint, HTTP request, manifest parsing and version comparison; the game
DLL only requests a check and reads a bounded status snapshot through appended
`DFSandboxAPI` callbacks. Those callbacks accept no URLs, paths or commands.
The native Windows transport uses system TLS verification, disallows HTTPS to
HTTP redirects, and bounds manifest size/time. On mobile and other desktops,
the same sandbox-owned checker uses the application's transport, including a
scoped updater transport in Lite without enabling online user levels. Checking
is asynchronous and duplicate in-flight requests are rejected.

Manifests may add a string `sandbox_version`, such as `"1.1.0"`, alongside the
existing `version`, `version_id`, `installer_urls` and `notes` fields. Either a
newer game or a newer sandbox can offer the complete platform package. Bundles
with explicit metadata that would downgrade either component are rejected.
Old manifests without `sandbox_version` continue to use game-version checks.
A publisher must ship the matching signed EXE/DLL pair in the package.

Desktop install/download requests save progress and close the game. The healthy
broker then re-reads its own configuration and re-checks the manifest; a mod
cannot supply an executable path, download URL or command line for that handoff.
On Windows it requires HTTPS and native user confirmation, starts the installed
updater helper outside the one-process job, and exits to release the old EXE.
The helper prohibits HTTPS downgrades, installs the complete package, and
reopens the installed launcher. Cancellation/failure leaves no arbitrary game
command executing in the broker; the user sees an error or the current game
reopens. Other desktops open the platform download outside the restricted child.
Mobile downloads continue through the OS browser. Update checks remain manual.
