// SPDX-License-Identifier: GPL-2.0-or-later

//! Vibration feedback for game events (csrc/dg_haptics.c picks the events).

use std::ffi::c_int;
use std::time::{Duration, Instant};

use haptics::HapticPattern;

haptics::use_api!();

/// Event kinds sent by csrc/dg_haptics.c, as `(kind, pattern, minimum gap)`.
/// The gap keeps rapid fire from turning into one continuous buzz.
const PATTERNS: &[(c_int, HapticPattern, u64)] = &[
    (1, HapticPattern::SharpClick60, 80),            // pistol, chaingun, fist
    (2, HapticPattern::StrongClick100, 150),         // shotguns, rockets, BFG
    (3, HapticPattern::SharpTickThree60, 80),        // plasma
    (4, HapticPattern::SoftFuzz60, 120),             // chainsaw
    (5, HapticPattern::DoubleClick100, 250),         // player hurt
    (6, HapticPattern::Alert750ms, 1000),            // player died
    (7, HapticPattern::SoftBump30, 60),              // item pickup
    (8, HapticPattern::SoftBump60, 60),              // weapon pickup
    (9, HapticPattern::DoubleClick60, 200),          // powerup
    (10, HapticPattern::SharpTickThree60, 60),       // menu open / select
];

struct State {
    api: Option<HapticsApi>,
    last: [Option<Instant>; PATTERNS.len()],
}

thread_local! {
    static STATE: std::cell::RefCell<Option<State>> = const { std::cell::RefCell::new(None) };
}

#[no_mangle]
extern "C" fn dg_haptic(kind: c_int) {
    let Some(index) = PATTERNS.iter().position(|&(k, _, _)| k == kind) else {
        return;
    };
    let (_, pattern, gap) = PATTERNS[index];

    STATE.with_borrow_mut(|state| {
        let state = state.get_or_insert_with(|| {
            let api = HapticsApi::try_new_with_timeout(Duration::from_millis(200));
            if api.is_none() {
                log::warn!("haptics server unavailable; no vibration");
            }
            State { api, last: [None; PATTERNS.len()] }
        });
        let Some(api) = &state.api else {
            return;
        };
        if state.last[index].is_some_and(|t| t.elapsed() < Duration::from_millis(gap)) {
            return;
        }
        state.last[index] = Some(Instant::now());
        #[cfg(not(target_os = "xous"))]
        log::info!("haptic: {pattern:?}");
        api.vibrate(pattern);
    });
}
