// SPDX-License-Identifier: GPL-2.0-or-later

//! Platform layer for doomgeneric: the `DG_*` hooks the C engine calls, plus
//! the thread that drives the game loop and hands frames to the Slint UI.

use std::collections::VecDeque;
use std::ffi::{c_char, c_int, c_uint, CStr, CString};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use slint_keyos_platform::slint::{self, Image, Rgb8Pixel, SharedPixelBuffer};

use crate::files::{self, Mode};
use crate::AppWindow;

pub const WIDTH: u32 = 320;
pub const HEIGHT: u32 = 200;

/// Stack for the engine thread. Doom recurses through the BSP and keeps some
/// large locals, so the platform default is too small.
const DOOM_STACK_SIZE: usize = 2 * 1024 * 1024;

extern "C" {
    static mut DG_ScreenBuffer: *mut u32;
    static mut key_nextweapon: c_int;
    static mut key_prevweapon: c_int;
    static mut key_fire: c_int;
    static mut key_use: c_int;
    static mut key_strafeleft: c_int;
    static mut key_straferight: c_int;
    static mut key_menu_confirm: c_int;
    /// doomtype.h `boolean`: an enum, one byte with ARM's short enums, so
    /// only the low byte is read.
    static menuactive: u8;
    fn M_SaveDefaults();
    fn doomgeneric_Create(argc: c_int, argv: *mut *mut c_char);
    fn doomgeneric_Tick();
}

static START: OnceLock<Instant> = OnceLock::new();
static KEYS: Mutex<VecDeque<(bool, u8)>> = Mutex::new(VecDeque::new());
static UI: OnceLock<slint::Weak<AppWindow>> = OnceLock::new();
/// Set while a frame is queued on the UI event loop, so a slow UI drops
/// frames instead of building a backlog.
static FRAME_PENDING: AtomicBool = AtomicBool::new(false);

/// Doom key codes from doomkeys.h that the on-screen controls send. On
/// device, the Rust side only uses the few it re-binds; the simulator's
/// script player (src/sim_debug.rs) uses the rest.
#[cfg_attr(target_os = "xous", allow(dead_code))]
pub mod keys {
    pub const RIGHT: u8 = 0xae;
    pub const LEFT: u8 = 0xac;
    pub const UP: u8 = 0xad;
    pub const DOWN: u8 = 0xaf;
    pub const STRAFE_L: u8 = 0xa0;
    pub const STRAFE_R: u8 = 0xa1;
    pub const USE: u8 = 0xa2;
    pub const FIRE: u8 = 0xa3;
    pub const ESCAPE: u8 = 27;
    pub const ENTER: u8 = 13;
    pub const TAB: u8 = 9;
    pub const NEXT_WEAPON: u8 = b']';
    pub const PREV_WEAPON: u8 = b'[';
}

/// Queues a key press or release for the engine.
pub fn key(code: u8, pressed: bool) {
    KEYS.lock().unwrap().push_back((pressed, code));
}

/// Starts the engine on its own thread. Frames are pushed to `ui`.
/// `preferred_wad` is tried before searching AppData and the Airlock.
pub fn start(ui: slint::Weak<AppWindow>, preferred_wad: Option<String>) {
    let start = *START.get_or_init(Instant::now);
    UI.set(ui).ok().expect("doom started twice");
    #[cfg(not(target_os = "xous"))]
    crate::sim_debug::start_script(start);
    #[cfg(target_os = "xous")]
    let _ = start;

    std::thread::Builder::new()
        .name("doom".into())
        .stack_size(DOOM_STACK_SIZE)
        .spawn(move || run(preferred_wad))
        .expect("spawn doom thread");
}

fn run(preferred_wad: Option<String>) {
    let Some(iwad) = files::find_iwad(preferred_wad) else {
        log::error!("no IWAD found in AppData or on the Airlock");
        let _ = UI.get().unwrap().upgrade_in_event_loop(|ui| ui.set_missing_wad(true));
        return;
    };
    log::info!("using IWAD {iwad}");

    let mut args = vec!["doom".to_string(), "-iwad".to_string(), iwad];
    // Extra engine flags for local runs, e.g. DOOM_ARGS="-warp 1 1".
    if let Ok(extra) = std::env::var("DOOM_ARGS") {
        args.extend(extra.split_whitespace().map(String::from));
    }
    // The engine keeps argv for the whole run, so the strings are leaked.
    let argv: Vec<*mut c_char> =
        args.into_iter().map(|a| CString::new(a).unwrap().into_raw()).collect();
    let argv = argv.leak();

    unsafe {
        doomgeneric_Create(argv.len() as c_int, argv.as_mut_ptr());
        bind_touch_keys();
        let mut menu_was_active = false;
        loop {
            doomgeneric_Tick();
            // Settings change in the menu, and the app can be closed without
            // going through Quit, so save them whenever the menu closes.
            let menu_active = std::ptr::read_volatile(&raw const menuactive) != 0;
            if menu_was_active && !menu_active {
                M_SaveDefaults();
            }
            menu_was_active = menu_active;
        }
    }
}

/// Points the engine's key bindings at the codes the touch buttons send. The
/// config file can't store keys outside its scancode table (fire, use,
/// strafe come back as 0), and vanilla Doom binds no weapon-cycle keys.
/// ENTER doubles as "yes" so prompts like Quit can be answered by touch.
unsafe fn bind_touch_keys() {
    key_fire = keys::FIRE as c_int;
    key_use = keys::USE as c_int;
    key_strafeleft = keys::STRAFE_L as c_int;
    key_straferight = keys::STRAFE_R as c_int;
    key_nextweapon = keys::NEXT_WEAPON as c_int;
    key_prevweapon = keys::PREV_WEAPON as c_int;
    key_menu_confirm = keys::ENTER as c_int;
}

#[no_mangle]
extern "C" fn DG_Init() {}

/// Logs how many frames the engine drew and the UI showed, every 10 s, so
/// device logs (`foundation logs`) show how the port performs.
fn count_frame(shown: bool) {
    static STATS: Mutex<(Option<Instant>, u32, u32)> = Mutex::new((None, 0, 0));
    let mut stats = STATS.lock().unwrap();
    let (since, drawn, shown_count) = &mut *stats;
    let since = since.get_or_insert_with(Instant::now);
    *drawn += 1;
    *shown_count += shown as u32;
    let elapsed = since.elapsed();
    if elapsed >= Duration::from_secs(10) {
        let secs = elapsed.as_secs_f32();
        log::info!("fps: engine {:.1}, shown {:.1}", *drawn as f32 / secs, *shown_count as f32 / secs);
        *stats = (Some(Instant::now()), 0, 0);
    }
}

#[no_mangle]
extern "C" fn DG_DrawFrame() {
    let busy = FRAME_PENDING.swap(true, Ordering::AcqRel);
    count_frame(!busy);
    if busy {
        return;
    }

    // The game is played in landscape on a portrait screen, so the frame is
    // rotated 90 degrees clockwise: source pixel (x, y) lands at
    // (HEIGHT - 1 - y, x) in a HEIGHT x WIDTH buffer.
    let mut frame = SharedPixelBuffer::<Rgb8Pixel>::new(HEIGHT, WIDTH);
    let src = unsafe {
        std::slice::from_raw_parts(DG_ScreenBuffer, (WIDTH * HEIGHT) as usize)
    };
    let dst = frame.make_mut_slice();
    for (y, row) in src.chunks_exact(WIDTH as usize).enumerate() {
        let col = HEIGHT as usize - 1 - y;
        for (x, &px) in row.iter().enumerate() {
            // i_video.c packs pixels as 0x00RRGGBB.
            dst[x * HEIGHT as usize + col] =
                Rgb8Pixel { r: (px >> 16) as u8, g: (px >> 8) as u8, b: px as u8 };
        }
    }

    #[cfg(not(target_os = "xous"))]
    crate::sim_debug::on_frame(&frame);

    let ui = UI.get().unwrap().clone();
    let posted = ui.upgrade_in_event_loop(move |ui| {
        ui.set_frame(Image::from_rgb8(frame));
        FRAME_PENDING.store(false, Ordering::Release);
    });
    if posted.is_err() {
        FRAME_PENDING.store(false, Ordering::Release);
    }
}

#[no_mangle]
extern "C" fn DG_SleepMs(ms: u32) {
    std::thread::sleep(Duration::from_millis(ms.into()));
}

#[no_mangle]
extern "C" fn DG_GetTicksMs() -> u32 {
    START.get_or_init(Instant::now).elapsed().as_millis() as u32
}

#[no_mangle]
extern "C" fn DG_GetKey(pressed: *mut c_int, key: *mut u8) -> c_int {
    match KEYS.lock().unwrap().pop_front() {
        Some((down, code)) => {
            unsafe {
                *pressed = down as c_int;
                *key = code;
            }
            1
        }
        None => 0,
    }
}

#[no_mangle]
extern "C" fn DG_SetWindowTitle(_title: *const c_char) {}

// WAD access for csrc/w_file_rust.c.

#[no_mangle]
extern "C" fn dg_wad_open(path: *const c_char, len: *mut c_uint) -> c_int {
    let path = unsafe { CStr::from_ptr(path) }.to_string_lossy();
    match files::open(&path, Mode::Read) {
        Some((handle, size)) => {
            log::info!("opened WAD {path} ({size} bytes)");
            unsafe { *len = size as c_uint };
            handle
        }
        None => {
            log::error!("cannot open WAD {path}");
            -1
        }
    }
}

#[no_mangle]
extern "C" fn dg_wad_read(handle: c_int, offset: c_uint, buffer: *mut u8, len: usize) -> usize {
    files::read_at(handle, offset.into(), unsafe { std::slice::from_raw_parts_mut(buffer, len) })
}

#[no_mangle]
extern "C" fn dg_wad_close(handle: c_int) {
    let _ = files::close(handle);
}
