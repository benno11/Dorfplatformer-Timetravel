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
