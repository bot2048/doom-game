# DOOM for Passport Prime

DOOM running as a native app on the [Foundation Passport Prime](https://foundation.xyz)
(KeyOS), played in landscape with one-finger touch controls.

- Engine: [doomgeneric](https://github.com/ozkl/doomgeneric) (GPL-2.0-or-later),
  vendored under `vendor/doomgeneric` at upstream commit `e257a09`.
- Game data: [Freedoom: Phase 1](https://freedoom.github.io) (BSD), downloaded
  by `tools/fetch_wad.sh`. Any Doom IWAD works (`doom1.wad`, `doom.wad`,
  `doom2.wad`, ...).

**Status:** plays fully in the KeyOS simulator: menus, movement, combat,
weapons, automap, save/load, settings that survive a restart, quitting. The
device build compiles and links. It has not been signed or run on real hardware
yet.

## Contents

1. [Prerequisites](#prerequisites)
2. [Quick start (simulator)](#quick-start-simulator)
3. [Controls](#controls)
4. [Running on a Passport Prime](#running-on-a-passport-prime)
5. [Tests](#tests)
6. [Debugging aids](#debugging-aids)
7. [How it works](#how-it-works)
8. [Project layout](#project-layout)
9. [Troubleshooting](#troubleshooting)
10. [Licensing](#licensing)

## Prerequisites

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
git clone https://github.com/<you>/doom-game.git
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

## Running on a Passport Prime

You need a Passport Prime on the KeyOS 1.4 beta with **Developer Mode**
enabled (Settings > Apps > Developer Mode).

1. **Linux only, once:** let your user talk to the device over USB. The rules
   file is in the SDK root that `foundation doctor` prints (by default
   `~/.foundation/sdk/current`).
   ```bash
   sudo cp ~/.foundation/sdk/current/share/99-passport.rules /etc/udev/rules.d/
   sudo udevadm control --reload && sudo udevadm trigger
   ```
2. **Once:** create a publisher signing identity, then name it in
   `app-config.toml` (`signing-identity = "<name>"`). Also update the
   `[publisher]` block, which still holds placeholders.
   ```bash
   foundation cert gen <name>
   ```
3. **Once per device:** allow your publisher certificate on the Passport.
   Compare the fingerprint on screen with the one `cert gen` printed.
   ```bash
   foundation cert install <name>
   ```
4. Build, sign, upload and launch:
   ```bash
   foundation sideload --release
   ```
5. Put the game data on the device: connect the Passport, set **Airlock** to
   Read/Write, and copy `wad/freedoom1.wad` (or your own IWAD) to the root of
   the Airlock drive. On first launch KeyOS asks to let DOOM read the Airlock;
   choose **Always Allow**.
6. Watch the logs while you play:
   ```bash
   foundation logs
   ```
   Every 10 seconds the app logs `fps: engine X, shown Y`.

To get an installable file instead of sideloading, run
`foundation pack --release` (after step 2). It writes
`target/keyos/doom.app`, which can be installed from the device's Settings.

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
  overlaps the left edge of the game.

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
| `DOOM_SCRIPT="..."` | Timed input: `<ms>*key` tap, `<ms>+key` / `<ms>-key` hold / release, `<ms>@name` snapshot. Keys: `enter esc up down left right fire use tab strafel strafer f2 f3` or one character. |

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
- **Engine changes.** Two small patches to the vendored engine, both marked
  "Passport port": `m_config.c` loads and saves settings when
  `DG_PERSIST_CONFIG` is defined (doomgeneric compiles that out), and
  `m_menu.c` names an empty save slot "SAVE n". `src/doom.rs` also re-binds the
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
| `src/sim_debug.rs` | Simulator-only scripted input and frame snapshots. |
| `src/theme.rs` | Theme wiring from the SDK template. |
| `csrc/libc_shim.c` | C library subset for the engine. |
| `csrc/dg_libc.h` | Force-included header: newlib macro fixes (device), `shim_*` renames (simulator). |
| `csrc/w_file_rust.c` | Engine WAD backend that streams through `src/files.rs`. |
| `ui/app.slint`, `ui/callbacks.slint` | Game view and touch controls. |
| `resources/` | App icon, theme, pre-rotated button labels. |
| `tools/fetch_wad.sh` | Downloads and verifies Freedoom. |
| `tools/gen_labels.py` | Regenerates the button label images. |
| `tests/` | libc shim tests. |
| `vendor/doomgeneric/` | The engine (upstream, plus the two patches above). |
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
  step 2 of [Running on a Passport Prime](#running-on-a-passport-prime).
  Everything before signing has already built.

## Licensing

The app as a whole is distributed under the **GNU GPL v2 or later** (see
`LICENSE`), because it links doomgeneric. Files written for this port carry
`GPL-2.0-or-later` headers. Files that come from the Foundation SDK app
template keep their original MIT headers. Freedoom has its own BSD-style
license (`wad/FREEDOOM-COPYING.txt`), and the game data itself is not in the
repository. DOOM is a trademark of id Software. This project is not affiliated
with id Software or Foundation Devices.
