# DOOM for Passport Prime

DOOM running as a native app on the [Foundation Passport Prime](https://foundation.xyz)
(KeyOS), played in landscape with one-finger touch controls.

- Engine: [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL-2.0-or-later),
  vendored under `vendor/doomgeneric` at upstream commit `e257a09`.
- Game data: [Freedoom: Phase 1](https://freedoom.github.io) (BSD), downloaded
  by `tools/fetch_wad.sh`. Any Doom IWAD works (`doom1.wad`, `doom.wad`,
  `doom2.wad`, ...).

**Status:** plays fully in the KeyOS simulator: menus, movement, combat,
weapons, automap, save/load, settings that survive a restart, quitting. A
signed build is available on the
[releases page](https://github.com/bot2048/doom-game/releases/latest). It has
not been tested on a real Passport yet, so please
[open an issue](https://github.com/bot2048/doom-game/issues) if something does
not work.

## Contents

1. [Install on your Passport](#install-on-your-passport)
2. [Controls](#controls)
3. [Sound and vibration](#sound-and-vibration)
4. [Building it yourself](#building-it-yourself)
5. [Quick start (simulator)](#quick-start-simulator)
6. [Building for a Passport Prime](#building-for-a-passport-prime)
7. [Tests](#tests)
8. [Debugging aids](#debugging-aids)
9. [How it works](#how-it-works)
10. [Project layout](#project-layout)
11. [Troubleshooting](#troubleshooting)
12. [Licensing](#licensing)

## Install on your Passport

This works on any Passport Prime running KeyOS 1.4.0 or newer (the version is
shown under **Settings > About**). You need a computer and a USB-C cable. You
do not need to build anything.

**1. Download these three files** from the
[latest release](https://github.com/bot2048/doom-game/releases/latest):

| File | What it is |
|---|---|
| `doom.app` | the app |
| `arpan-jain.crt` | the publisher certificate, which tells your Passport who made the app |
| `freedoom1.wad` | the game data ([Freedoom](https://freedoom.github.io), free to share) |

**2. Copy them to the Passport.** Connect the Passport to your computer with
the USB-C cable and set the Airlock to **Read/Write** on the Passport. The
Airlock then shows up on your computer as a USB drive. Copy all three files to
the top level of that drive (not into a folder), eject the drive, and unplug
the cable. A USB stick plugged into the Passport also works for the `.app` and
`.crt` files, but the game data has to be on the Airlock.

**3. Allow the publisher (once).** On the Passport, open
**Settings > Apps > Allowed Publishers > Add Publisher** and pick
`arpan-jain.crt`. Check that the fingerprint on screen is exactly

```text
85a3e6b06f8c4b4f8e27ee5bea8af8311e38bfd0cd55f61b6ab5997eb1651908
```

(short form `85a3e6b0…b1651908`), then tap **Allow this Publisher**. KeyOS
warns that Foundation has not verified this publisher. It shows that warning
for every app Foundation did not make. If you already allowed this publisher
for [Sanctm](https://github.com/bot2048/sanctm), skip this step.

**4. Install the app.** Open **Settings > Apps > Install App** and pick
`doom.app`. DOOM appears in the launcher.

**5. Play.** Open DOOM and turn the Passport sideways: the game is drawn in
landscape, with the top of the game along the right edge of the screen in
portrait. The first time, KeyOS asks whether DOOM may read Airlock files;
choose **Always Allow**, because the game reads `freedoom1.wad` from there
every time it starts. Leave the file on the Airlock. If the game shows
"No WAD file found", the file is missing from the top level of the Airlock or
the access was declined.

Any other Doom game file works the same way: copy `doom1.wad` (the free
shareware episode), `doom.wad`, `doom2.wad`, `plutonia.wad` or `tnt.wad` to the
Airlock. If several are present, `freedoom1.wad` is used first.

**Updating.** Download the newer `doom.app` and install it the same way. Your
saves and settings are kept.

## Building it yourself

Everything below is for building the app from source. To just play it, use
[Install on your Passport](#install-on-your-passport).

### Prerequisites

- Linux x86_64 (the SDK also ships for macOS; these steps were run on Ubuntu).
- The **Foundation SDK** (tested with 1.1.0, KeyOS 1.4 target), installed so
  that `foundation` is on your `PATH`. The SDK brings its own Nix flake with
  the Rust toolchain (`armv7a-unknown-xous-elf`), `arm-none-eabi-gcc` and
  the simulator.
- **Nix** with flakes (the SDK installer sets this up).
- `curl`, `unzip`, `sha256sum` for fetching the game data.
- About **6 GB of free disk space** for the first build (Slint and the KeyOS
  crates are large).
- Optional: Python 3 with Pillow, only to regenerate button labels.

Check the environment:

```bash
foundation doctor
```

If it reports the Nix shell or the KeyOS target as missing, enter the SDK
shell first. Every `foundation` command below is run inside it:

```bash
foundation develop
```

## Quick start (simulator)

```bash
git clone https://github.com/bot2048/doom-game.git
cd doom-game
tools/fetch_wad.sh        # downloads and verifies wad/freedoom1.wad
foundation develop        # enter the SDK shell (once per terminal)
foundation sim            # builds the app and opens the Passport simulator
```

The first build takes several minutes. DOOM opens straight away in the
simulator window. The game is drawn in landscape on the portrait screen:
the simulator's **right edge is the top** of the game.

To play a different IWAD in the simulator:

```bash
DOOM_WAD=/path/to/doom2.wad foundation sim
```

## Controls

KeyOS gives apps a single pointer, so everything is designed for one finger
(or the mouse in the simulator).

| Control | Where (landscape) | What it does |
|---|---|---|
| Joystick | lower left | Press anywhere in the area and drag: up/down walk, left/right turn. Drag further to run. |
| FIRE | lower right | Hold to shoot. Drag sideways while holding to turn and aim. |
| USE | left of FIRE | Open doors, press switches. |
| « » | above USE | Strafe left / right. |
| MENU | top left | Open the menu (Escape). Also "no" in prompts. |
| MAP | top | Toggle the automap. |
| ENTER | top | Confirm menu items. Also "yes" in prompts (quit, end game, Nightmare). |
| WPN - / WPN + | top right | Previous / next weapon. |

Saving: MENU, Save Game, pick a slot, ENTER, ENTER. Empty slots are named
"SAVE n" automatically (there is no keyboard).

## Sound and vibration

KeyOS has no audio output for apps, so the game is silent. Doom's sound
code is compiled out (`FEATURE_SOUND` is not defined), and the SDK has no
API for playing sound. Instead, the events you would hear most are turned
into vibration through the KeyOS haptics API:

| Event | Vibration |
|---|---|
| Pistol, chaingun, fist | short sharp click |
| Shotguns, rocket launcher, BFG | strong click |
| Plasma rifle | light tick |
| Chainsaw | soft buzz |
| Taking damage | double click |
| Dying | long alert |
| Item, weapon, powerup pickups | soft bumps, a double click for powerups |
| Opening and confirming menus | light tick |

Each kind is rate-limited so rapid fire doesn't become one continuous buzz.
Only sounds made by you count; enemy gunfire and title-screen demos don't
vibrate. To turn vibration off, set **Options > Sound Volume > SFX Volume**
to 0 (the setting is saved).

The simulator has no vibration motor: there, each vibration is logged as
`haptic: <pattern>` instead.

## Building for a Passport Prime

This sideloads your own build over USB, which needs **Developer Mode** on the
Passport (Settings > Apps > Developer Mode).

1. **Linux only, once:** let your user talk to the device over USB. The rules
   file is in the SDK root that `foundation doctor` prints (by default
   `~/.foundation/sdk/current`).
   ```bash
   sudo cp ~/.foundation/sdk/current/share/99-passport.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
2. **Once:** create your own publisher signing identity, then put its name in
   `app-config.toml` (`signing-identity = "<name>"`) and your details in the
   `[publisher]` block. The identity in this repository belongs to the
   maintainer, and its private key is not published.
   ```bash
   foundation cert gen <name>
   ```
   A Passport treats an app signed by a different publisher as a different
   app, so your build will not install over the released `doom.app`. Remove
   the released one first.
3. **Once per device:** allow your publisher certificate on the Passport.
   Compare the fingerprint on screen with the one `cert gen` printed.
   ```bash
   foundation cert install <name>
   ```
4. Build, sign, upload and launch:
   ```bash
   foundation sideload --release
   ```
5. Copy `wad/freedoom1.wad` (or your own IWAD) to the top level of the
   Airlock, as in [Install on your Passport](#install-on-your-passport).
6. Watch the logs while you play:
   ```bash
   foundation logs
   ```
   Every 10 seconds the app logs `fps: engine X, shown Y`.

To get an installable file instead of sideloading, run
`foundation pack --release` (after step 2). It writes
`target/keyos/doom.app`, which installs from **Settings > Apps > Install App**
without Developer Mode.

### Where the app looks for files on the device

| File | Read from | Written to |
|---|---|---|
| IWAD (`*.wad`) | app storage (AppData), then the Airlock root | never |
| Settings (`.default.cfg`, `.doomgenericdoom.cfg`) | AppData | AppData, whenever the menu closes and on quit |
| Saves (`.savegame/doomsavN.dsg`) | AppData | AppData |

Only `.wad` files are ever looked up on the Airlock, so KeyOS asks for Airlock
access at most once per launch. If no IWAD is found, the app shows a message
explaining how to add one.

### Things to check on first hardware run

These could not be verified without a device:

- that KeyOS loads the binary (it is a PIE with no text relocations);
- performance (`fps:` log lines) and memory (Doom reserves 6 MB for its zone);
- the 2 MB stack of the engine thread (`DOOM_STACK_SIZE` in `src/doom.rs`);
- reading the 28 MB WAD from the Airlock, and whether the KeyOS status bar
  overlaps the left edge of the game;
- how the vibration patterns feel, and whether their rate limits in
  `src/haptics.rs` need tuning.

## Tests

```bash
tests/run_shim_test.sh
rustc --edition 2021 --test src/buffered.rs -o /tmp/buffered_test && /tmp/buffered_test
```

- `tests/run_shim_test.sh` builds the C library shim for the host and checks
  `snprintf`, `sscanf`, `fscanf`, `feof`, `fseek`/`ftell`, `atoi`/`atof` and
  the string functions against glibc, including reading a config file the way
  the engine does.
- `src/buffered.rs` has unit tests for the buffered file handle (byte-wise
  reads and writes are batched, seek/tell, read-after-write).

End-to-end checks run in the simulator with scripted input (next section).

## Debugging aids

Simulator-only environment variables (see `src/sim_debug.rs`):

| Variable | Effect |
|---|---|
| `DOOM_WAD=<path>` | IWAD to load from the host filesystem. |
| `DOOM_ARGS="-warp 1 1"` | Extra engine arguments. |
| `DOOM_FRAME_DUMP=<file.ppm>` | Writes the current frame about once a second. |
| `DOOM_SNAP_DIR=<dir>` | Where `@name` script steps write `<name>.ppm`. |
| `DOOM_SCRIPT="..."` | Timed input: `<ms>*key` tap, `<ms>+key` / `<ms>-key` hold / release, `<ms>@name` snapshot. Keys: `enter esc up down left right fire use tab strafel strafer f2 f3 f4` or one character. |

Example: start a new game, fire, and take a snapshot:

```bash
DOOM_SNAP_DIR=/tmp DOOM_SCRIPT="3000*enter 3600*enter 4200*enter 4800*enter 7000*fire 7600@shot" foundation sim
```

Frames are stored rotated (portrait). Rotate them 90° counterclockwise to view.

## How it works

- **Engine thread.** doomgeneric runs on its own thread (`src/doom.rs`). Its
  `DG_*` hooks provide the clock, sleep, key events and frames. Each 320x200
  frame is rotated 90° into a Slint image and handed to the UI thread, which
  scales it 2.375x to fill the landscape screen.
- **Touch input.** The Slint UI (`ui/app.slint`) is laid out in landscape
  coordinates and mapped onto the 480x800 portrait screen. Buttons queue Doom
  key codes. The KeyOS Slint renderer cannot rotate text, so button labels
  are images pre-rotated by `tools/gen_labels.py`.
- **No libc on KeyOS.** The engine is compiled with `arm-none-eabi-gcc` against
  newlib's headers, but newlib's `libc.a` cannot be linked (the KeyOS target
  is position independent). `csrc/libc_shim.c` implements the ~50 libc
  functions the engine uses. Memory, files, console output and exit are
  forwarded to Rust (`src/libc.rs`). The simulator runs the same shim, renamed
  to `shim_*` by `csrc/dg_libc.h`, so its code paths match the device.
- **Files.** `src/files.rs` maps the engine's paths onto KeyOS storage (see the
  table above). Every KeyOS file call is an IPC message, and the engine reads
  and writes savegames one byte at a time, so `src/buffered.rs` batches reads
  and writes and tracks the file position locally.
- **Haptics.** `s_sound.c` calls `DG_SoundHook` (`csrc/dg_haptics.c`) for
  every sound the game starts. It picks out the player's own sounds and
  hands them to `src/haptics.rs`, which sends rate-limited KeyOS haptic
  patterns.
- **Engine changes.** Three small patches to the vendored engine, all marked
  "Passport port": `m_config.c` loads and saves settings when
  `DG_PERSIST_CONFIG` is defined (doomgeneric compiles that out),
  `m_menu.c` names an empty save slot "SAVE n", and `s_sound.c` calls the
  haptics hook. `src/doom.rs` also re-binds the
  keys the touch UI sends after the config loads, because the config format
  cannot store some of them.

## Project layout

| Path | Purpose |
|---|---|
| `app-config.toml` | App manifest: ID, name, icon, permissions, publisher. |
| `Cargo.toml`, `Cargo.lock` | Rust package; KeyOS crates come from the SDK via `.foundation-sdk/`. |
| `build.rs` | Compiles the engine and shim with `cc` (device and host flags) and the Slint UI. |
| `src/main.rs` | App entry point. |
| `src/doom.rs` | Engine thread, `DG_*` hooks, key bindings, frame hand-off, fps log. |
| `src/files.rs` | KeyOS file access, IWAD discovery. |
| `src/buffered.rs` | Buffered, position-tracking file handle (unit tested). |
| `src/libc.rs` | Rust side of the libc shim: allocator, console, files, exit. |
| `src/haptics.rs` | Vibration patterns and rate limits for game events. |
| `src/sim_debug.rs` | Simulator-only scripted input and frame snapshots. |
| `src/theme.rs` | Theme wiring from the SDK template. |
| `csrc/libc_shim.c` | C library subset for the engine. |
| `csrc/dg_libc.h` | Force-included header: newlib macro fixes (device), `shim_*` renames (simulator). |
| `csrc/w_file_rust.c` | Engine WAD backend that streams through `src/files.rs`. |
| `csrc/dg_haptics.c` | Picks which game sounds become vibrations. |
| `ui/app.slint`, `ui/callbacks.slint` | Game view and touch controls. |
| `resources/` | App icon, theme, pre-rotated button labels. |
| `tools/fetch_wad.sh` | Downloads and verifies Freedoom. |
| `tools/gen_labels.py` | Regenerates the button label images. |
| `tests/` | libc shim tests. |
| `vendor/doomgeneric/` | The engine (upstream, plus the three patches above). |
| `wad/` | Game data (not in git) and the Freedoom license. |
| `AGENTS.md`, `.mcp.json`, `permission_templates.toml` | From the SDK template: agent notes, the `passport-drive` MCP server, permission bundles. |

Generated and ignored: `target/`, `.foundation-sdk/`, `ui/ui`, `manifest.toml`,
and the SDK agent skills in `.claude/` and `.agents/` (`foundation skills`
reinstalls them).

## Troubleshooting

- **`foundation doctor` says the Nix shell or KeyOS target is missing.** Run
  `foundation develop` and build from inside that shell.
- **"No space left on device" during the build.** The first build needs about
  6 GB. `foundation clean` removes build output. Builds with
  `CARGO_PROFILE_DEV_DEBUG=0` are much smaller.
- **The app shows "No WAD file found".** In the simulator, run
  `tools/fetch_wad.sh` or set `DOOM_WAD`. On the device, copy an IWAD to the
  Airlock root and allow Airlock access when asked.
- **The simulator keeps running after you close its terminal.** Stop it with
  `pkill -f '[k]eyos/simulator/'`.
- **`foundation build` stops with "No signing identity is configured".** See
  step 2 of [Building for a Passport Prime](#building-for-a-passport-prime).
  Everything before signing has already built.

## Licensing

The app as a whole is distributed under the **GNU GPL v2 or later** (see
`LICENSE`), because it links doomgeneric. Files written for this port carry
`GPL-2.0-or-later` headers. Files that come from the Foundation SDK app
template keep their original MIT headers. Freedoom has its own BSD-style
license (`wad/FREEDOOM-COPYING.txt`), and the game data itself is not in the
repository. DOOM is a trademark of id Software. This project is not affiliated
with id Software or Foundation Devices.
