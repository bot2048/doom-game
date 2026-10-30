// SPDX-License-Identifier: GPL-2.0-or-later

//! Simulator-only test aids, driven by environment variables:
//!
//! - `DOOM_FRAME_DUMP=<file.ppm>` writes the current frame about once a second.
//! - `DOOM_SNAP_DIR=<dir>` is where `snap` script steps write frames.
//! - `DOOM_SCRIPT="<step> ..."` feeds timed input, so a game can be played
//!   without touching the screen. Each step is `<ms><op><arg>`, timed from
//!   engine start: `*key` taps a key, `+key` / `-key` press and release it,
//!   `@name` saves the next frame as `<DOOM_SNAP_DIR>/<name>.ppm`.
//!   Keys: enter esc up down left right fire use tab strafel strafer f2 f3,
//!   or any single character. Example: `DOOM_SCRIPT="3000*enter 3500@menu"`.

use std::sync::Mutex;
use std::time::{Duration, Instant};

use slint_keyos_platform::slint::{Rgb8Pixel, SharedPixelBuffer};

use crate::doom::{self, keys};

static SNAP: Mutex<Option<String>> = Mutex::new(None);

fn key_code(name: &str) -> Option<u8> {
    Some(match name {
        "enter" => keys::ENTER,
        "esc" => keys::ESCAPE,
        "up" => keys::UP,
        "down" => keys::DOWN,
        "left" => keys::LEFT,
        "right" => keys::RIGHT,
        "fire" => keys::FIRE,
        "use" => keys::USE,
        "tab" => keys::TAB,
        "strafel" => keys::STRAFE_L,
        "strafer" => keys::STRAFE_R,
        "f2" => 0x80 + 0x3c,
        "f3" => 0x80 + 0x3d,
        _ if name.len() == 1 => name.as_bytes()[0],
        _ => return None,
    })
}

/// Starts the `DOOM_SCRIPT` player, if the variable is set.
pub fn start_script(start: Instant) {
    let Ok(script) = std::env::var("DOOM_SCRIPT") else {
        return;
    };
    let mut steps = Vec::new();
    for step in script.split_whitespace() {
        let Some(split) = step.find(['*', '+', '-', '@']) else {
            log::warn!("bad script step {step}");
            continue;
        };
        let (at, rest) = step.split_at(split);
        let (op, arg) = rest.split_at(1);
        let Ok(at) = at.parse::<u64>() else {
            log::warn!("bad script time in {step}");
            continue;
        };
        steps.push((Duration::from_millis(at), op.to_string(), arg.to_string()));
    }
    steps.sort_by_key(|s| s.0);

    std::thread::spawn(move || {
        for (at, op, arg) in steps {
            if let Some(wait) = at.checked_sub(start.elapsed()) {
                std::thread::sleep(wait);
            }
            log::info!("script: {op}{arg}");
            if op == "@" {
                *SNAP.lock().unwrap() = Some(arg);
                continue;
            }
            let Some(code) = key_code(&arg) else {
                log::warn!("script: unknown key {arg}");
                continue;
            };
            match op.as_str() {
                "+" => doom::key(code, true),
                "-" => doom::key(code, false),
                _ => {
                    doom::key(code, true);
                    std::thread::sleep(Duration::from_millis(120));
                    doom::key(code, false);
                }
            }
        }
    });
}

fn write_ppm(path: &str, frame: &SharedPixelBuffer<Rgb8Pixel>) {
    let mut out = format!("P6\n{} {}\n255\n", frame.width(), frame.height()).into_bytes();
    out.extend(frame.as_slice().iter().flat_map(|p| [p.r, p.g, p.b]));
    if let Err(e) = std::fs::write(path, out) {
        log::warn!("cannot write {path}: {e}");
    }
}

/// Called with every frame the engine draws.
pub fn on_frame(frame: &SharedPixelBuffer<Rgb8Pixel>) {
    if let Some(name) = SNAP.lock().unwrap().take() {
        let dir = std::env::var("DOOM_SNAP_DIR").unwrap_or_else(|_| ".".into());
        write_ppm(&format!("{dir}/{name}.ppm"), frame);
    }

    static LAST: Mutex<Option<Instant>> = Mutex::new(None);
    let Ok(path) = std::env::var("DOOM_FRAME_DUMP") else {
        return;
    };
    let mut last = LAST.lock().unwrap();
    if last.is_some_and(|t| t.elapsed() < Duration::from_secs(1)) {
        return;
    }
    *last = Some(Instant::now());
    write_ppm(&path, frame);
}
