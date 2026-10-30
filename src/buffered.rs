// SPDX-License-Identifier: GPL-2.0-or-later

//! Buffered file handle used by `files`. Plain std, so it is unit tested on
//! the host: `rustc --edition 2021 --test src/buffered.rs -o /tmp/t && /tmp/t`.

use std::io::{Read, Seek, SeekFrom, Write};

pub trait Stream: Read + Write + Seek {}
impl<T: Read + Write + Seek> Stream for T {}

/// Read cache and write buffer size. Every call into a KeyOS file is a
/// message to the filesystem server, and the engine reads and writes
/// savegames a byte at a time.
const BUFFER: usize = 16 * 1024;

/// A file with its position tracked here, so seek and tell (which the
/// savegame code calls constantly) never reach the filesystem server, and
/// small reads and writes are batched.
pub struct Handle {
    inner: Box<dyn Stream>,
    /// Logical position and length of the file.
    pos: u64,
    pub len: u64,
    /// Bytes of the file starting at `cache_start`.
    cache: Vec<u8>,
    cache_start: u64,
    /// Writes not yet sent, starting at `pending_start`.
    pending: Vec<u8>,
    pending_start: u64,
}

impl Handle {
    pub fn new(inner: Box<dyn Stream>, len: u64, pos: u64) -> Self {
        Self { inner, pos, len, cache: Vec::new(), cache_start: 0, pending: Vec::new(), pending_start: 0 }
    }

    pub fn flush(&mut self) -> std::io::Result<()> {
        if !self.pending.is_empty() {
            self.inner.seek(SeekFrom::Start(self.pending_start))?;
            self.inner.write_all(&self.pending)?;
            self.pending.clear();
        }
        Ok(())
    }

    /// Reads from the file itself at `pos` until `buf` is full or EOF.
    fn read_through(&mut self, pos: u64, buf: &mut [u8]) -> usize {
        if self.inner.seek(SeekFrom::Start(pos)).is_err() {
            return 0;
        }
        let mut done = 0;
        while done < buf.len() {
            match self.inner.read(&mut buf[done..]) {
                Ok(0) | Err(_) => break,
                Ok(n) => done += n,
            }
        }
        done
    }

    pub fn read(&mut self, buf: &mut [u8]) -> usize {
        if self.flush().is_err() {
            return 0;
        }
        if buf.len() >= BUFFER {
            let n = self.read_through(self.pos, buf);
            self.pos += n as u64;
            return n;
        }
        let cached = self.cache_start..self.cache_start + self.cache.len() as u64;
        if !cached.contains(&self.pos) || self.pos + buf.len() as u64 > cached.end {
            let mut cache = std::mem::take(&mut self.cache);
            cache.resize(BUFFER, 0);
            let n = self.read_through(self.pos, &mut cache);
            cache.truncate(n);
            self.cache = cache;
            self.cache_start = self.pos;
        }
        let offset = (self.pos - self.cache_start) as usize;
        let n = buf.len().min(self.cache.len().saturating_sub(offset));
        buf[..n].copy_from_slice(&self.cache[offset..offset + n]);
        self.pos += n as u64;
        n
    }

    pub fn write(&mut self, buf: &[u8]) -> usize {
        self.cache.clear();
        if !self.pending.is_empty() && self.pos != self.pending_start + self.pending.len() as u64 {
            if self.flush().is_err() {
                return 0;
            }
        }
        if self.pending.is_empty() {
            self.pending_start = self.pos;
        }
        self.pending.extend_from_slice(buf);
        self.pos += buf.len() as u64;
        self.len = self.len.max(self.pos);
        if self.pending.len() >= BUFFER && self.flush().is_err() {
            return 0;
        }
        buf.len()
    }

    pub fn seek(&mut self, pos: SeekFrom) -> Option<u64> {
        let new = match pos {
            SeekFrom::Start(p) => Some(p),
            SeekFrom::Current(d) => self.pos.checked_add_signed(d),
            SeekFrom::End(d) => self.len.checked_add_signed(d),
        }?;
        self.pos = new;
        Some(new)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static CALLS: AtomicUsize = AtomicUsize::new(0);

    /// A Cursor that counts calls, standing in for a KeyOS file.
    struct Counted(Cursor<Vec<u8>>);
    impl Read for Counted {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            CALLS.fetch_add(1, Ordering::Relaxed);
            // KeyOS reads return at most a page at a time.
            let n = buf.len().min(4096);
            self.0.read(&mut buf[..n])
        }
    }
    impl Write for Counted {
        fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
            CALLS.fetch_add(1, Ordering::Relaxed);
            self.0.write(buf)
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    impl Seek for Counted {
        fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
            CALLS.fetch_add(1, Ordering::Relaxed);
            self.0.seek(pos)
        }
    }

    fn handle(data: Vec<u8>) -> Handle {
        let len = data.len() as u64;
        Handle::new(Box::new(Counted(Cursor::new(data))), len, 0)
    }

    fn contents(h: &mut Handle) -> Vec<u8> {
        h.flush().unwrap();
        let mut out = vec![0; h.len as usize];
        h.seek(SeekFrom::Start(0));
        let mut done = 0;
        while done < out.len() {
            match h.read(&mut out[done..]) {
                0 => break,
                n => done += n,
            }
        }
        out.truncate(done);
        out
    }

    #[test]
    fn byte_reads_match_and_are_batched() {
        let data: Vec<u8> = (0..100_000u32).map(|i| (i * 7 % 251) as u8).collect();
        let mut h = handle(data.clone());
        CALLS.store(0, Ordering::Relaxed);
        let mut out = Vec::new();
        let mut b = [0u8; 1];
        while h.read(&mut b) == 1 {
            out.push(b[0]);
        }
        assert_eq!(out, data);
        assert!(CALLS.load(Ordering::Relaxed) < 100, "{} calls", CALLS.load(Ordering::Relaxed));
    }

    #[test]
    fn tell_is_free_and_correct() {
        let mut h = handle(vec![1, 2, 3, 4, 5]);
        let mut b = [0u8; 2];
        assert_eq!(h.read(&mut b), 2);
        CALLS.store(0, Ordering::Relaxed);
        assert_eq!(h.seek(SeekFrom::Current(0)), Some(2));
        assert_eq!(h.seek(SeekFrom::End(0)), Some(5));
        assert_eq!(h.seek(SeekFrom::Current(-1)), Some(4));
        assert_eq!(h.seek(SeekFrom::Current(-10)), None);
        assert_eq!(CALLS.load(Ordering::Relaxed), 0);
        assert_eq!(h.read(&mut b), 1);
        assert_eq!(b[0], 5);
        assert_eq!(h.read(&mut b), 0);
    }

    #[test]
    fn byte_writes_match_and_are_batched() {
        let mut h = handle(Vec::new());
        CALLS.store(0, Ordering::Relaxed);
        let data: Vec<u8> = (0..50_000u32).map(|i| (i % 253) as u8).collect();
        for &byte in &data {
            assert_eq!(h.write(&[byte]), 1);
            // savegame padding asks for the position constantly
            assert!(h.seek(SeekFrom::Current(0)).is_some());
        }
        h.flush().unwrap();
        assert!(CALLS.load(Ordering::Relaxed) < 20, "{} calls", CALLS.load(Ordering::Relaxed));
        assert_eq!(h.len, data.len() as u64);
        assert_eq!(contents(&mut h), data);
    }

    #[test]
    fn mixed_seek_write_read() {
        let mut h = handle(b"hello world".to_vec());
        let mut b = [0u8; 5];
        assert_eq!(h.read(&mut b), 5);
        assert_eq!(&b, b"hello");
        h.seek(SeekFrom::Start(6));
        assert_eq!(h.write(b"WORLD"), 5);
        h.seek(SeekFrom::Start(0));
        assert_eq!(h.write(b"J"), 1);
        // a read after writes must see them
        h.seek(SeekFrom::Start(0));
        let mut all = [0u8; 11];
        assert_eq!(h.read(&mut all), 11);
        assert_eq!(&all, b"Jello WORLD");
        h.seek(SeekFrom::End(0));
        assert_eq!(h.write(b"!"), 1);
        assert_eq!(contents(&mut h), b"Jello WORLD!");
    }

    #[test]
    fn write_past_end_extends() {
        let mut h = handle(b"ab".to_vec());
        h.seek(SeekFrom::Start(4));
        h.write(b"z");
        assert_eq!(h.len, 5);
        assert_eq!(contents(&mut h), b"ab\0\0z");
    }

    #[test]
    fn large_reads_bypass_cache() {
        let data: Vec<u8> = (0..200_000u32).map(|i| (i % 241) as u8).collect();
        let mut h = handle(data.clone());
        let mut out = vec![0u8; 150_000];
        h.seek(SeekFrom::Start(1000));
        let mut done = 0;
        while done < out.len() {
            match h.read(&mut out[done..]) {
                0 => break,
                n => done += n,
            }
        }
        assert_eq!(done, out.len());
        assert_eq!(&out[..], &data[1000..151_000]);
        // and a small read right after still lines up
        let mut b = [0u8; 3];
        assert_eq!(h.read(&mut b), 3);
        assert_eq!(&b, &data[151_000..151_003]);
    }
}
