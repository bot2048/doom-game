// SPDX-License-Identifier: GPL-2.0-or-later

//! File access for the engine thread.
//!
//! Reads look in the app's private storage (AppData) first, then on the
//! Airlock, where a WAD copied over USB lands. Writes (config, saves) always go
//! to AppData. In the simulator an absolute host path is also accepted, so the
//! WAD can be read straight from the project directory.

use std::cell::RefCell;
use std::io::SeekFrom;

use crate::buffered::Handle;

use crate::fs_permissions::fs::{Location, OpenFlags};
use crate::{File, FileSystem};

/// IWADs the engine knows how to run, in order of preference.
const IWADS: &[&str] =
    &["freedoom1.wad", "doom1.wad", "doom.wad", "doom2.wad", "freedoom2.wad", "plutonia.wad", "tnt.wad"];

/// Where reads look. Only WADs are searched for on the Airlock: touching it
/// makes KeyOS ask the user for access, which is only worth it for game data.
fn read_locations(path: &str) -> &'static [Location] {
    if path.to_ascii_lowercase().ends_with(".wad") {
        &[Location::AppData, Location::Airlock]
    } else {
        &[Location::AppData]
    }
}

thread_local! {
    static FS: FileSystem = FileSystem::default();
    static OPEN: RefCell<Vec<Option<Handle>>> = const { RefCell::new(Vec::new()) };
}

#[derive(Debug)]
pub enum Mode {
    Read,
    Write,
    Append,
    ReadWrite,
}

/// Turns the engine's relative paths ("./default.cfg", ".//.savegame/x")
/// into the form the KeyOS filesystem expects.
fn normalize(path: &str) -> String {
    path.split('/').filter(|part| !part.is_empty() && *part != ".").collect::<Vec<_>>().join("/")
}

fn open_keyos(path: &str, mode: &Mode) -> Option<Handle> {
    let path = normalize(path);
    FS.with(|fs| {
        let opened = match mode {
            Mode::Read => {
                let found = read_locations(&path).iter().find_map(|&loc| {
                    let file = fs.open_file(path.as_str(), loc, OpenFlags::READ_ONLY).ok()?;
                    log::info!("reading {path} from {loc:?}");
                    Some(file)
                });
                if found.is_none() {
                    log::info!("{path} not found");
                }
                found
            }
            Mode::ReadWrite => fs
                .open_file(path.as_str(), Location::AppData, OpenFlags::READ_WRITE)
                .map_err(|e| log::warn!("cannot open {path} for {mode:?}: {e:?}"))
                .ok(),
            Mode::Write | Mode::Append => {
                let _ = fs.ensure_parent_dir_exists(&path, Location::AppData);
                fs.open_file(path.as_str(), Location::AppData, OpenFlags::CREATE)
                    .map_err(|e| log::warn!("cannot open {path} for {mode:?}: {e:?}"))
                    .ok()
            }
        };
        let mut file: File = opened?;
        if matches!(mode, Mode::Write) {
            if let Err(e) = file.truncate() {
                log::warn!("cannot truncate {path}: {e:?}");
                return None;
            }
        }
        if !matches!(mode, Mode::Read) {
            log::info!("opened {path} in AppData for {mode:?}");
        }
        let len = if matches!(mode, Mode::Write) { 0 } else { file.metadata().map(|m| m.size).unwrap_or(0) };
        let pos = if matches!(mode, Mode::Append) { len } else { 0 };
        Some(Handle::new(Box::new(file), len, pos))
    })
}

#[cfg(not(target_os = "xous"))]
fn open_host(path: &str, mode: &Mode) -> Option<Handle> {
    if !path.starts_with('/') || !matches!(mode, Mode::Read) {
        return None;
    }
    let file = std::fs::File::open(path).ok()?;
    let len = file.metadata().ok()?.len();
    Some(Handle::new(Box::new(file), len, 0))
}

/// Opens a file and returns its handle and length.
pub fn open(path: &str, mode: Mode) -> Option<(i32, u64)> {
    #[cfg(not(target_os = "xous"))]
    let handle = open_host(path, &mode).or_else(|| open_keyos(path, &mode));
    #[cfg(target_os = "xous")]
    let handle = open_keyos(path, &mode);

    let handle = handle?;
    let len = handle.len;
    OPEN.with_borrow_mut(|open| {
        let slot = open.iter().position(Option::is_none).unwrap_or_else(|| {
            open.push(None);
            open.len() - 1
        });
        open[slot] = Some(handle);
        Some((slot as i32, len))
    })
}

fn with_handle<R>(handle: i32, default: R, f: impl FnOnce(&mut Handle) -> R) -> R {
    OPEN.with_borrow_mut(|open| match open.get_mut(handle as usize) {
        Some(Some(h)) => f(h),
        _ => default,
    })
}

/// Reads up to `buf.len()` bytes at the current position.
pub fn read(handle: i32, buf: &mut [u8]) -> usize {
    with_handle(handle, 0, |h| {
        let mut done = 0;
        while done < buf.len() {
            match h.read(&mut buf[done..]) {
                0 => break,
                n => done += n,
            }
        }
        done
    })
}

pub fn read_at(handle: i32, offset: u64, buf: &mut [u8]) -> usize {
    if with_handle(handle, None, |h| h.seek(SeekFrom::Start(offset))).is_none() {
        return 0;
    }
    read(handle, buf)
}

pub fn write(handle: i32, buf: &[u8]) -> usize {
    with_handle(handle, 0, |h| h.write(buf))
}

/// Seeks like fseek (whence 0 = start, 1 = current, 2 = end) and returns the
/// new position, or -1.
pub fn seek(handle: i32, offset: i64, whence: i32) -> i64 {
    let pos = match whence {
        0 if offset < 0 => return -1,
        0 => SeekFrom::Start(offset as u64),
        1 => SeekFrom::Current(offset),
        _ => SeekFrom::End(offset),
    };
    with_handle(handle, -1, |h| h.seek(pos).map_or(-1, |p| p as i64))
}

/// Closes a handle, writing out anything buffered. False if that failed.
pub fn close(handle: i32) -> bool {
    OPEN.with_borrow_mut(|open| {
        let Some(mut h) = open.get_mut(handle as usize).and_then(Option::take) else {
            return false;
        };
        match h.flush() {
            Ok(()) => true,
            Err(e) => {
                log::error!("write failed on close: {e}");
                false
            }
        }
    })
}

pub fn remove(path: &str) -> bool {
    FS.with(|fs| fs.remove(normalize(path), Location::AppData).is_ok())
}

pub fn rename(from: &str, to: &str) -> bool {
    FS.with(|fs| fs.rename(normalize(from), normalize(to), Location::AppData).is_ok())
}

pub fn mkdir(path: &str) -> bool {
    FS.with(|fs| fs.create_dir(normalize(path), Location::AppData).is_ok())
}

/// Names of the files at the root of `location`. Listing the Airlock asks
/// the user for access once; if they decline, the list is empty.
fn list_root(location: Location) -> Vec<String> {
    FS.with(|fs| {
        let dir = match fs.open_dir("/", location) {
            Ok(dir) => dir,
            Err(e) => {
                log::info!("cannot list {location:?}: {e:?}");
                return Vec::new();
            }
        };
        let mut names = Vec::new();
        while let Ok(Some(entry)) = dir.next_entry() {
            if entry.is_file {
                names.push(entry.name);
            }
        }
        names
    })
}

/// Finds an IWAD to play: `preferred` if it opens (simulator host path),
/// otherwise the first known IWAD in AppData, then on the Airlock. Names are
/// matched without regard to case, since the Airlock is a FAT drive.
pub fn find_iwad(preferred: Option<String>) -> Option<String> {
    if let Some(path) = preferred {
        if let Some((handle, _)) = open(&path, Mode::Read) {
            let _ = close(handle);
            return Some(path);
        }
    }
    for location in [Location::AppData, Location::Airlock] {
        let names = list_root(location);
        let found = IWADS.iter().find_map(|wanted| names.iter().find(|n| n.eq_ignore_ascii_case(wanted)));
        if let Some(name) = found {
            log::info!("found IWAD {name} in {location:?}");
            return Some(name.clone());
        }
    }
    None
}
