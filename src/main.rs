// SPDX-FileCopyrightText: 2026 Foundation Devices, Inc. <hello@foundation.xyz>
// SPDX-License-Identifier: MIT

mod buffered;
mod doom;
mod files;
mod libc;
#[cfg(not(target_os = "xous"))]
mod sim_debug;
mod theme;

use slint_keyos_platform::app_ui2;

app_ui2!("DOOM");

/// The simulator reads the WAD straight from the project directory (or
/// `DOOM_WAD`). On device the engine searches AppData and the Airlock.
fn preferred_wad() -> Option<String> {
    if cfg!(target_os = "xous") {
        return None;
    }
    Some(
        std::env::var("DOOM_WAD")
            .unwrap_or_else(|_| concat!(env!("CARGO_MANIFEST_DIR"), "/wad/freedoom1.wad").to_string()),
    )
}

fn app_main(_cx: AppContext, ui: AppWindow) {
    log_server::init_wait(env!("CARGO_CRATE_NAME")).unwrap();
    log::set_max_level(log::LevelFilter::Info);

    theme::init(&ui);

    ui.global::<Callbacks>().on_key(|code, pressed| {
        doom::key(code as u8, pressed);
    });

    doom::start(ui.as_weak(), preferred_wad());

    ui.run().expect("UI running");
}
