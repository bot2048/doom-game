// SPDX-License-Identifier: GPL-2.0-or-later

//! Rust half of csrc/libc_shim.c: the C library services KeyOS lacks.
//! The simulator uses the console, file and exit hooks too; memory
//! allocation comes from the host libc there.

use std::ffi::{c_char, c_int, c_long, c_void, CStr};
use std::sync::Mutex;

use crate::files::{self, Mode};

// Every allocation carries its size in a header so free/realloc can rebuild
// the Layout. 8 bytes keeps the payload 8-byte aligned, as malloc must.
#[cfg(target_os = "xous")]
const HEADER: usize = 8;
#[cfg(target_os = "xous")]
const ALIGN: usize = 8;

#[cfg(target_os = "xous")]
unsafe fn allocate(size: usize, zeroed: bool) -> *mut c_void {
    use std::alloc::{alloc, alloc_zeroed, Layout};
    let Ok(layout) = Layout::from_size_align(size + HEADER, ALIGN) else {
        return std::ptr::null_mut();
    };
    let base = if zeroed { alloc_zeroed(layout) } else { alloc(layout) };
    if base.is_null() {
        return std::ptr::null_mut();
    }
    (base as *mut usize).write(size);
    base.add(HEADER) as *mut c_void
}

#[cfg(target_os = "xous")]
unsafe fn header(ptr: *mut c_void) -> (*mut u8, usize) {
    let base = (ptr as *mut u8).sub(HEADER);
    (base, (base as *const usize).read())
}

#[cfg(target_os = "xous")]
#[no_mangle]
unsafe extern "C" fn malloc(size: usize) -> *mut c_void {
    allocate(size, false)
}

#[cfg(target_os = "xous")]
#[no_mangle]
unsafe extern "C" fn calloc(count: usize, size: usize) -> *mut c_void {
    match count.checked_mul(size) {
        Some(total) => allocate(total, true),
        None => std::ptr::null_mut(),
    }
}

#[cfg(target_os = "xous")]
#[no_mangle]
unsafe extern "C" fn free(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    let (base, size) = header(ptr);
    std::alloc::dealloc(base, std::alloc::Layout::from_size_align_unchecked(size + HEADER, ALIGN));
}

#[cfg(target_os = "xous")]
#[no_mangle]
unsafe extern "C" fn realloc(ptr: *mut c_void, new_size: usize) -> *mut c_void {
    if ptr.is_null() {
        return allocate(new_size, false);
    }
    let (base, size) = header(ptr);
    let old = std::alloc::Layout::from_size_align_unchecked(size + HEADER, ALIGN);
    let base = std::alloc::realloc(base, old, new_size + HEADER);
    if base.is_null() {
        return std::ptr::null_mut();
    }
    (base as *mut usize).write(new_size);
    base.add(HEADER) as *mut c_void
}

#[no_mangle]
extern "C" fn dg_exit(status: c_int) -> ! {
    let line = std::mem::take(&mut *LINE.lock().unwrap());
    if !line.is_empty() {
        log::info!("{}", String::from_utf8_lossy(&line));
    }
    log::info!("engine exited with status {status}");
    // Log lines go to a separate log server; give the last ones (config
    // save, errors) a moment to arrive before the process disappears.
    std::thread::sleep(std::time::Duration::from_millis(200));
    std::process::exit(status)
}

static LINE: Mutex<Vec<u8>> = Mutex::new(Vec::new());

/// Console output, split into log lines.
#[no_mangle]
unsafe extern "C" fn dg_console_write(buf: *const c_char, len: usize) {
    let bytes = std::slice::from_raw_parts(buf as *const u8, len);
    let mut line = LINE.lock().unwrap();
    for &b in bytes {
        if b == b'\n' {
            log::info!("{}", String::from_utf8_lossy(&line));
            line.clear();
        } else {
            line.push(b);
        }
    }
}

unsafe fn path(p: *const c_char) -> String {
    CStr::from_ptr(p).to_string_lossy().into_owned()
}

#[no_mangle]
unsafe extern "C" fn dg_file_open(p: *const c_char, mode: c_int) -> c_int {
    let mode = match mode {
        1 => Mode::Write,
        2 => Mode::Append,
        3 => Mode::ReadWrite,
        _ => Mode::Read,
    };
    files::open(&path(p), mode).map_or(-1, |(handle, _)| handle)
}

#[no_mangle]
unsafe extern "C" fn dg_file_read(handle: c_int, buf: *mut c_void, len: usize) -> usize {
    files::read(handle, std::slice::from_raw_parts_mut(buf as *mut u8, len))
}

#[no_mangle]
unsafe extern "C" fn dg_file_write(handle: c_int, buf: *const c_void, len: usize) -> usize {
    files::write(handle, std::slice::from_raw_parts(buf as *const u8, len))
}

#[no_mangle]
extern "C" fn dg_file_seek(handle: c_int, offset: c_long, whence: c_int) -> c_long {
    files::seek(handle, offset.into(), whence) as c_long
}

#[no_mangle]
extern "C" fn dg_file_close(handle: c_int) -> c_int {
    if files::close(handle) { 0 } else { -1 }
}

#[no_mangle]
unsafe extern "C" fn dg_file_remove(p: *const c_char) -> c_int {
    if files::remove(&path(p)) { 0 } else { -1 }
}

#[no_mangle]
unsafe extern "C" fn dg_file_rename(from: *const c_char, to: *const c_char) -> c_int {
    if files::rename(&path(from), &path(to)) { 0 } else { -1 }
}

#[no_mangle]
unsafe extern "C" fn dg_mkdir(p: *const c_char) -> c_int {
    if files::mkdir(&path(p)) { 0 } else { -1 }
}
