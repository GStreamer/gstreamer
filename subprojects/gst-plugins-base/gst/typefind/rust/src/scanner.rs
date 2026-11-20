// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
// Copyright (C) 2026 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use crate::bindings;
use crate::scan::{Pattern, find_any};
use std::marker::PhantomData;
use std::num::NonZeroU64;
use std::ptr;

#[repr(transparent)]
pub struct TypeFind(pub(crate) bindings::GstTypeFind);

impl TypeFind {
    pub fn peek_var(&mut self, offset: i64, size: u32) -> Option<&[u8]> {
        assert!(size > 0);

        // SAFETY: a successful peek grants `size` readable bytes, valid until
        // the finder returns.
        unsafe {
            let data = bindings::gst_type_find_peek(&mut self.0, offset, size);
            if data.is_null() {
                None
            } else {
                Some(std::slice::from_raw_parts(data, size as usize))
            }
        }
    }

    pub fn peek<const S: usize>(&mut self, offset: i64) -> Option<&[u8; S]> {
        assert!(S <= u32::MAX as usize);
        assert!(S > 0);

        // SAFETY: a successful peek grants `S` readable bytes, valid until the
        // finder returns.
        unsafe {
            let data = bindings::gst_type_find_peek(&mut self.0, offset, S as u32);
            if data.is_null() {
                None
            } else {
                Some(&*(data as *const [u8; S]))
            }
        }
    }

    /// Returns the data stream length in bytes, or None if unknown.
    ///
    /// Per the C API: `0` means "not available". A seekable stream states
    /// its total size; a pushed one states the bytes gathered so far, a
    /// known bound on what peeks can reach but not the whole file.
    pub fn length(&mut self) -> Option<NonZeroU64> {
        // SAFETY: `self.0` is the live `GstTypeFind`.
        let raw = unsafe { bindings::gst_type_find_get_length(&mut self.0) };
        NonZeroU64::new(raw)
    }

    /// Peek up to `max` bytes from offset 0, clamped by the known stream length.
    ///
    /// When the stream length is unknown (C API returns 0), peeks exactly `max`
    /// bytes. When known and less than `max`, peeks all available data instead of
    /// wasting a call on more bytes than exist.
    pub fn peek_at_most(&mut self, max: usize) -> Option<&[u8]> {
        let len = self
            .length()
            .map_or(max as u64, NonZeroU64::get)
            .min(max as u64)
            .min(u32::MAX as u64);
        if len == 0 {
            return None;
        }
        self.peek_var(0, len as u32)
    }

    /// Peek between `min` and `max` bytes from offset 0, None when fewer than
    /// `min` are available. States the smallest header a typefinder can judge,
    /// so the conditions below it need no length checks.
    pub fn peek_between(&mut self, min: usize, max: usize) -> Option<&[u8]> {
        let data = self.peek_at_most(max)?;
        (data.len() >= min).then_some(data)
    }

    /// Peek as many bytes as `magic` is long from offset 0 and compare.
    /// Returns `true` only when enough data is available and matches.
    ///
    /// An empty `magic` matches nothing: the engine cannot answer a
    /// zero-length peek.
    pub fn starts_with(&mut self, magic: &[u8]) -> bool {
        !magic.is_empty()
            && matches!(self.peek_var(0, magic.len() as u32), Some(data) if data == magic)
    }
}

/// Cached window size of `Scanner`, in bytes.
const WINDOW: usize = 4096;

/// Zero-copy windowed cursor over the typefind stream, in the spirit of
/// `std::io::BufRead`: `peek()` serves bytes from a cached window and only
/// queries the engine again when the window cannot cover the request;
/// `consume()` advances.
///
/// A scanner is `Copy`, and a copy is an independent cursor over the same
/// stream. That is what keeps a speculative walk from dragging the scan along
/// with it: hand a copy to the part that runs off looking at frames, and the
/// original stays where it was. Peeking only ever reads, and a window is valid
/// until the typefinding function returns, so any number of cursors may share
/// one stream.
#[derive(Clone, Copy)]
pub struct Scanner<'a> {
    tf: *mut bindings::GstTypeFind,
    // The stream a scanner reads only lives while the typefinding call that
    // handed it out is running.
    stream: PhantomData<&'a TypeFind>,
    base: *const u8, // cached window
    offset: i64,     // stream offset of `base`
    len: usize,      // window length
    pos: usize,      // cursor within the window
}

impl<'a> Scanner<'a> {
    pub fn new(tf: &'a mut TypeFind, offset: i64) -> Self {
        Self {
            tf: &raw mut tf.0,
            stream: PhantomData,
            base: ptr::null(),
            offset,
            len: 0,
            pos: 0,
        }
    }

    /// Serve exactly `min` bytes at the cursor, querying the engine again
    /// when the cached window cannot cover them. `None` when fewer than
    /// `min` bytes remain in the stream.
    pub fn peek(&mut self, min: usize) -> Option<&[u8]> {
        self.peek_at_least(min).map(|window| &window[..min])
    }

    /// The bytes the cursor can serve without querying the engine again: at
    /// least `min` of them, at most one window. `None` when fewer than `min`
    /// bytes remain in the stream.
    ///
    /// A finder that has to search the stream asks for the header it needs
    /// and searches the whole slice, instead of peeking that header at every
    /// offset. The slice stays valid until the typefinding function returns,
    /// so it may be searched and then handed back through [`consume`].
    ///
    /// [`consume`]: Scanner::consume
    pub fn peek_at_least(&mut self, min: usize) -> Option<&[u8]> {
        if min == 0 {
            return Some(&[]);
        }
        if self.len.saturating_sub(self.pos) < min {
            let offset = self.offset + self.pos as i64;
            let (base, len) = self.refill(offset, min)?;
            self.base = base;
            self.offset = offset;
            self.len = len;
            self.pos = 0;
        }
        // SAFETY: a window is documented to stay valid until the typefinding
        // function returns, and the engine only ever appends to its buffer
        // cache, so a window outlives any number of later peeks from any
        // number of scanners. `refill` yields at least `min` bytes or nothing,
        // never a short window, so the bytes are there to hand out.
        Some(unsafe { std::slice::from_raw_parts(self.base.add(self.pos), self.len - self.pos) })
    }

    /// Move the cursor to the next position at or before `end` where one of
    /// `patterns` matches, leaving it on the first octet of the match.
    /// `false` when the stream ends first, with the cursor left where the
    /// search stopped.
    ///
    /// The window is searched whole rather than a header peeked at every
    /// offset: the last octets of a window are left for the next one, so a
    /// match opening there is still in view complete.
    pub(crate) fn find<const N: usize, const M: usize>(
        &mut self,
        end: i64,
        patterns: &[Pattern<N>; M],
    ) -> bool {
        while self.offset() <= end {
            let reach = usize::try_from(end - self.offset())
                .unwrap_or(usize::MAX)
                .saturating_add(1);
            let (span_len, hit) = {
                let Some(window) = self.peek_at_least(N) else {
                    return false;
                };
                let span = &window[..reach.saturating_add(N - 1).min(window.len())];
                let hit = find_any(patterns, span);
                (span.len(), hit)
            };
            match hit {
                Some(at) => {
                    self.consume(at);
                    return true;
                }
                None => self.consume(span_len.saturating_sub(N - 1)),
            }
        }
        false
    }

    /// Declare the next `n` bytes consumed. May run past the window end; the
    /// next `peek` queries the engine at the right offset.
    pub fn consume(&mut self, n: usize) {
        // `n` may come from an untrusted header field; saturating keeps the
        // cursor inside usize where such a field would otherwise overflow it.
        self.pos = self.pos.saturating_add(n);
    }

    /// A cursor `n` bytes behind this one. A search leaves the cursor on the
    /// octet carrying a start code value, so a caller that has to look at the
    /// whole syntactic element from its beginning needs to step back over the
    /// prefix first.
    pub fn before(&self, n: usize) -> Self {
        let mut earlier = *self;
        earlier.offset = self
            .offset()
            .checked_sub(n as i64)
            .expect("callers step back only over bytes already read");
        // Drop the cached window rather than index into it at the wrong place.
        earlier.base = ptr::null();
        earlier.len = 0;
        earlier.pos = 0;
        earlier
    }

    /// Current stream position of the cursor.
    pub fn offset(&self) -> i64 {
        self.offset + self.pos as i64
    }

    /// Query the engine for up to `WINDOW` bytes at `offset`, falling back
    /// to whatever the stream has left (at least `min`) for short tails.
    fn refill(&mut self, offset: i64, min: usize) -> Option<(*const u8, usize)> {
        // A stream that cannot serve `min` bytes answers nothing, decided
        // before asking: a read starting at or past the end of a known
        // stream reports a broken read to the engine rather than nothing,
        // and a cursor may well walk there — a frame that runs to the last
        // octet leaves the cursor exactly at the end.
        // SAFETY: `self.tf` is the live `GstTypeFind`.
        let left = NonZeroU64::new(unsafe { bindings::gst_type_find_get_length(self.tf) })
            .map_or(u64::MAX, NonZeroU64::get)
            .checked_sub(offset.max(0) as u64)?;
        if left < min as u64 {
            return None;
        }
        // Short tail: the stream ends before a full window, so a full-window
        // read would reach past its end. Ask for the remainder instead, an
        // exact request the engine can serve whole.
        let chunk = WINDOW.max(min).min(left as usize) as u32;
        // SAFETY: a successful peek grants `chunk` readable bytes, valid until
        // the finder returns.
        let base = unsafe { bindings::gst_type_find_peek(self.tf, offset, chunk) };
        (!base.is_null()).then_some((base, chunk as usize))
    }
}
