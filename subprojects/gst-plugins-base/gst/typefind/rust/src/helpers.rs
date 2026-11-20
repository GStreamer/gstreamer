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

use crate::bindings::{self, gpointer};
use crate::scan::{Pattern, find_any};
use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::num::NonZeroU64;
use std::os::raw::{c_char, c_uint};
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
            .min(max as u64);
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
        earlier.offset = self.offset() - n as i64;
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

/// Registered typefind function ABI, C signature
/// `void (*) (GstTypeFind *tf, gpointer user_data)`. TypeFind is a
/// repr(transparent) wrapper, so `&mut TypeFind` has pointer ABI.
pub type TypeFindFunc = extern "C" fn(&mut TypeFind, gpointer);

/// What a [`Feature`] does when the engine asks it about a stream.
pub(crate) enum Finder {
    /// Nothing: the row answers by extension only.
    None,
    /// A finder of its own.
    Func(TypeFindFunc),
    /// The shared magic-prefix finder, with this row's pattern and confidence.
    StartWith {
        magic: &'static [&'static [u8]],
        prob: TypeFindProbability,
    },
}

/// One row of the registration table.
pub(crate) struct Feature {
    pub(crate) name: &'static CStr,
    pub(crate) rank: c_uint,
    pub(crate) extensions: Option<&'static CStr>,
    pub(crate) caps: Option<&'static CStr>,
    pub(crate) finder: Finder,
}

impl Finder {
    /// The C function and private data to register for this row.
    ///
    /// The shared finder is only reachable from [`Finder::StartWith`], so the
    /// engine can never call it with a pointer that is not a row.
    pub(crate) fn registration(&self, row: &'static Feature) -> (Option<TypeFindFunc>, gpointer) {
        match self {
            Finder::None => (None, std::ptr::null_mut()),
            Finder::Func(func) => (Some(*func), std::ptr::null_mut()),
            Finder::StartWith { .. } => (
                Some(start_with_type_find),
                row as *const Feature as gpointer,
            ),
        }
    }
}

/// Typefind function for the [`Finder::StartWith`] rows.
///
/// The engine hands back the row registration passed, so the row supplies both
/// the pattern to match and the name to suggest.
extern "C" fn start_with_type_find(tf: &mut TypeFind, private: gpointer) {
    // SAFETY: `Finder::registration` pairs this function with a `&'static
    // Feature` and nothing else, and the engine passes that pointer back
    // unchanged.
    let feature = unsafe { &*(private as *const Feature) };
    let Finder::StartWith { magic, prob } = &feature.finder else {
        unreachable!("start_with_type_find registered for a non-StartWith row")
    };
    if magic.iter().any(|pattern| tf.starts_with(pattern)) {
        suggest!(tf, *prob, feature.name);
    }
}

/// 16-bit little-endian value at the start of `d`. The caller's peek
/// guarantees the length.
pub(crate) fn u16le(d: &[u8]) -> u16 {
    u16::from_le_bytes([d[0], d[1]])
}

/// 16-bit big-endian value at the start of `d`. The caller's peek guarantees
/// the length.
pub(crate) fn u16be(d: &[u8]) -> u16 {
    u16::from_be_bytes([d[0], d[1]])
}

/// 32-bit little-endian value at the start of `d`. The caller's peek
/// guarantees the length.
pub(crate) fn u32le(d: &[u8]) -> u32 {
    u32::from_le_bytes([d[0], d[1], d[2], d[3]])
}

/// 32-bit big-endian value at the start of `d`. The caller's peek guarantees
/// the length.
pub(crate) fn u32be(d: &[u8]) -> u32 {
    u32::from_be_bytes([d[0], d[1], d[2], d[3]])
}

/// Allows to enforce a specific type for the argument.
///
/// Helper function for the macros below.
pub(crate) fn type_check<T: ?Sized>(_v: &T) {}

/// Maps a type name literal to the matching fundamental `GType` id.
macro_rules! map_type_name {
    (i32) => {
        $crate::bindings::G_TYPE_INT
    };
    (str) => {
        $crate::bindings::G_TYPE_STRING
    };
    (bool) => {
        $crate::bindings::G_TYPE_BOOLEAN
    };
}

/// Typefind probability / confidence level.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(transparent)]
pub struct TypeFindProbability(c_uint);

impl TypeFindProbability {
    pub const NONE: Self = Self(bindings::GST_TYPE_FIND_NONE);
    pub const MINIMUM: Self = Self(bindings::GST_TYPE_FIND_MINIMUM);
    pub const POSSIBLE: Self = Self(bindings::GST_TYPE_FIND_POSSIBLE);
    pub const LIKELY: Self = Self(bindings::GST_TYPE_FIND_LIKELY);
    pub const NEARLY_CERTAIN: Self = Self(bindings::GST_TYPE_FIND_NEARLY_CERTAIN);
    pub const MAXIMUM: Self = Self(bindings::GST_TYPE_FIND_MAXIMUM);

    /// Wraps a raw `GstTypeFindProbability` value, for the few finders that
    /// report a confidence between the named levels.
    pub(crate) const fn from_raw(v: c_uint) -> Self {
        Self(v)
    }

    #[inline]
    pub fn get(self) -> c_uint {
        self.0
    }
}

impl From<c_uint> for TypeFindProbability {
    #[inline]
    fn from(v: c_uint) -> Self {
        Self(v)
    }
}

impl From<TypeFindProbability> for c_uint {
    #[inline]
    fn from(p: TypeFindProbability) -> Self {
        p.0
    }
}

/// Maps a type name literal and value to the value used in the call.
macro_rules! map_value {
    (i32, $val:expr) => {{
        $crate::helpers::type_check::<i32>(&$val);
        $val
    }};
    (str, $val:expr) => {{
        $crate::helpers::type_check::<std::ffi::CStr>(&$val);
        $val.as_ptr()
    }};
    (bool, $val:expr) => {{
        $crate::helpers::type_check::<bool>(&$val);
        if $val { 1i32 } else { 0i32 }
    }};
}

/// Suggest a media type and optional caps fields to the typefind engine.
///
/// The probability can be a named constant or any numeric literal:
/// ```ignore
/// // Named constants (from `helpers::TypeFindProbability`)
/// suggest!(tf, Prob::MAXIMUM, c"image/bmp");
///
/// // Raw numeric value (arbitrary confidence)
/// suggest!(tf, 90, c"application/x-ape");
/// ```
macro_rules! suggest {
    ($tf:expr, $prob:expr, $name:expr $(,)?) => {{
        // SAFETY: `$tf` is live and `$name` outlives the call.
        unsafe {
            $crate::bindings::gst_type_find_suggest_simple(
                &mut $tf.0,
                $crate::helpers::TypeFindProbability::from($prob).into(),
                {
                    $crate::helpers::type_check::<std::ffi::CStr>(&$name);
                    $name
                }.as_ptr(),
                std::ptr::null::<::std::os::raw::c_char>(),
            );
        }
    }};
    ($tf:expr, $prob:expr, $name:expr, [ $( ($key:expr, $type:ident : $val:expr) , )*] $(,)?) => {
        let _p = $crate::helpers::TypeFindProbability::from($prob).into();
        // SAFETY: `$tf` is live; `$name` and the keys outlive the call.
        unsafe {
            $crate::bindings::gst_type_find_suggest_simple(
                &mut $tf.0,
                _p,
                {
                    $crate::helpers::type_check::<std::ffi::CStr>(&$name);
                    $name
                }.as_ptr(),
                $(
                    {
                        $crate::helpers::type_check::<std::ffi::CStr>(&$key);
                        $key
                    }.as_ptr(),
                    $crate::helpers::map_type_name!($type),
                    $crate::helpers::map_value!($type, $val),
                )*
                std::ptr::null::<::std::os::raw::c_char>(),
            );
        }
    };
}

/// `none` expands to `None` (a C NULL argument), anything else
/// to `Some(..)`. Used by [`typefinder!`].
macro_rules! opt {
    (none) => {
        None
    };
    ($s:expr) => {
        Some($s)
    };
}

/// One row of the registration table (typefindfunctions.rs::FEATURES).
/// The strings are `&'static CStr` literals, so registration hands them to C
/// without copying.
/// Caps default to the feature name; verify each row against the C caps string.
///
/// - `typefinder!(c"image/bmp", PRIMARY, bmp_type_find, c"bmp")`
///   extensions "bmp", caps = feature name (default)
/// - `typefinder!(c"audio/x-gsm", PRIMARY, none, c"gsm", c"audio/x-gsm, rate=8000, channels=1")`
///   NULL func, so nothing is recognised from content and only the extension
///   answers; caps given explicitly because they differ from the name
/// - `typefinder!(c"image/tiff", PRIMARY, tiff_type_find, c"tif,tiff", c"image/tiff, endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }")`
///   caps differing from the feature name
/// - `typefinder!(c"image/x-icon", MARGINAL, windows_icon_type_find, none, none)`
///   NULL extensions and NULL caps
///
/// Ranks: PRIMARY/SECONDARY/MARGINAL (or offsets like `PRIMARY+103`).
/// `none` = C NULL argument. 3-arg form defaults to caps = name,
/// extensions = None.
macro_rules! typefinder {
    ($name:expr, $rank:expr, $func:expr) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: None,
            caps: Some($name),
            finder: Finder::Func($func),
        }
    };
    // `none` in the function position has to be matched as a literal, exactly as
    // in the extensions and caps positions below: captured as an expression it no
    // longer matches the literal, and the row would carry a function that does
    // not exist. Only the form with an extension is here, because a factory with
    // neither a function nor an extension can never answer for anything.
    ($name:expr, $rank:expr, none, $exts:expr, $caps:expr) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: opt!($exts),
            caps: opt!($caps),
            finder: Finder::None,
        }
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: Some($exts),
            caps: Some($name),
            finder: Finder::Func($func),
        }
    };
    // Ahead of the arms that capture `$exts` or `$caps`: `none` parses as an
    // expression there, and a captured expression no longer matches the
    // literal `none` pattern.
    ($name:expr, $rank:expr, $func:expr, none, none) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: None,
            caps: None,
            finder: Finder::Func($func),
        }
    };
    ($name:expr, $rank:expr, $func:expr, none, $caps:expr) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: None,
            caps: opt!($caps),
            finder: Finder::Func($func),
        }
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr, $caps:expr) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: opt!($exts),
            caps: opt!($caps),
            finder: Finder::Func($func),
        }
    };
}

/// A [`typefinder!`] row that uses [`start_with_type_find`]: the format is
/// recognised by the magic bytes at offset 0 and nothing else.
///
/// `start_with!(c"video/x-flv", SECONDARY, c"flv", [b"FLV"], Prob::MAXIMUM)`
/// expands to the row that pairs the shared finder with this pattern and
/// confidence. `none` in the extension position means no extension.
///
/// The advertised caps are always the feature name, which is also what the
/// finder suggests; a format needing caps fields of its own does not fit here.
macro_rules! start_with {
    // `none` has to be matched as a literal here for the same reason as in
    // `typefinder!`: captured as an expression it no longer matches the
    // literal pattern, and `opt!` would build `Some(none)`.
    ($name:expr, $rank:expr, none, [$($magic:expr),+ $(,)?], $prob:expr $(,)?) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: None,
            caps: Some($name),
            finder: Finder::StartWith {
                magic: &[$($magic as &'static [u8]),+],
                prob: $prob,
            },
        }
    };
    ($name:expr, $rank:expr, $exts:expr, [$($magic:expr),+ $(,)?], $prob:expr $(,)?) => {
        Feature {
            name: $name,
            rank: $rank,
            extensions: Some($exts),
            caps: Some($name),
            finder: Finder::StartWith {
                magic: &[$($magic as &'static [u8]),+],
                prob: $prob,
            },
        }
    };
}

/// Converts `data` from `from_codeset` to UTF-8, replacing unconvertible bytes with '*'.
/// Strips any UTF-8 BOM that the converter passes through. Returns None on converter error.
pub(crate) fn convert_to_utf8(data: &[u8], from_codeset: &str) -> Option<Vec<u8>> {
    let from = CString::new(from_codeset).ok()?;
    // SAFETY: `data` and `from` are valid for the call; GLib owns the result.
    let bytes = unsafe {
        let mut error: *mut bindings::GError = ptr::null_mut();

        let result = bindings::g_convert_with_fallback(
            data.as_ptr().cast(),
            data.len() as isize,
            c"UTF-8".as_ptr(),
            from.as_ptr(),
            c"*".as_ptr(),
            ptr::null_mut(),
            ptr::null_mut(),
            &mut error,
        );
        if result.is_null() {
            if !error.is_null() {
                bindings::g_error_free(error);
            }
            return None;
        }

        let bytes = CStr::from_ptr(result).to_bytes().to_vec();
        bindings::g_free(result.cast());

        bytes
    };

    Some(
        bytes
            .strip_prefix(b"\xef\xbb\xbf")
            .unwrap_or(&bytes)
            .to_vec(),
    )
}

/// The charset of the current locale and whether it is UTF-8.
pub(crate) fn locale_charset() -> Option<(String, bool)> {
    // SAFETY: `g_get_charset` returns a static NUL-terminated string or NULL.
    unsafe {
        let mut charset: *const c_char = ptr::null();
        let is_utf8 = bindings::g_get_charset(&mut charset);
        if charset.is_null() {
            return None;
        }

        Some((
            CStr::from_ptr(charset).to_string_lossy().into_owned(),
            is_utf8 != 0,
        ))
    }
}

// ---------------------------------------------------------------------------
// Safe wrappers over the pbutils AudioSpecificConfig readers. Each needs at
// least two octets of config; a shorter one reads as no name, no channels and
// no rate.
// ---------------------------------------------------------------------------

/// A name pbutils settled on, or nothing when it could not decide. The borrow is
/// unbounded because both callers hand back a string literal or an entry of a
/// function-local static table, never a pointer into the config passed in.
fn named(ptr: *const c_char) -> Option<&'static CStr> {
    // SAFETY: pbutils returns a static string, never a pointer into `asc`.
    (!ptr.is_null()).then(|| unsafe { CStr::from_ptr(ptr) })
}

/// Profile, as pbutils names it.
pub(crate) fn aac_profile(asc: &[u8]) -> Option<&'static CStr> {
    // SAFETY: `asc` is valid for `asc.len()` bytes.
    named(unsafe { bindings::gst_codec_utils_aac_get_profile(asc.as_ptr(), asc.len() as c_uint) })
}

/// Level, as pbutils computes it from the speaker count, the rate and the
/// complexity.
///
/// shortcut: inherits the codec-utils level defects - it can return AAC Profile
/// levels 6 and 7, which Table 1.10 of 14496-3 does not define, and it drops
/// the LFE element for channel configuration 6. Upstream report pending;
/// parity with every other AAC caps producer comes first.
pub(crate) fn aac_level(asc: &[u8]) -> Option<&'static CStr> {
    // SAFETY: `asc` is valid for `asc.len()` bytes.
    named(unsafe { bindings::gst_codec_utils_aac_get_level(asc.as_ptr(), asc.len() as c_uint) })
}

/// Speaker count, or 0 when the layout is left to a program_config_element.
/// Configuration 7 is eight speakers.
pub(crate) fn aac_channels(asc: &[u8]) -> u32 {
    // SAFETY: `asc` is valid for `asc.len()` bytes.
    unsafe { bindings::gst_codec_utils_aac_get_channels(asc.as_ptr(), asc.len() as c_uint) }
}

/// Sampling frequency in Hz, or 0 when the config does not carry one.
pub(crate) fn aac_sample_rate(asc: &[u8]) -> u32 {
    // SAFETY: `asc` is valid for `asc.len()` bytes.
    unsafe { bindings::gst_codec_utils_aac_get_sample_rate(asc.as_ptr(), asc.len() as c_uint) }
}

// Alias for callers who want the short name.
pub(crate) use TypeFindProbability as Prob;

// Re-export macros and items needed by typefindfunctions.rs.
pub(crate) use {map_type_name, map_value, opt, start_with, suggest, typefinder};

/// A real typefind context, for tests that drive a finder.
///
/// `GstTypeFind` is a struct the caller of a typefinding function builds
/// and owns, and libgstreamer's `gst_type_find_peek`,
/// `gst_type_find_get_length` and `gst_type_find_suggest_simple` are thin
/// dispatchers over its callbacks (gst/gsttypefind.c). So a test builds
/// the context itself: the callbacks read the bytes its test handed over
/// and record the suggestion, and everything between the finder and the
/// callbacks runs the real libgstreamer code — the variadic
/// `suggest_simple` included, which marshals the caps field values a Rust
/// shim could never read. Per-run state lives in the `Probe` `probe`
/// owns, reached only through the context's own `data` pointer: the seam
/// the C API offers for exactly this. Suggesting builds a `GstCaps`,
/// whose type registers at `gst_init`, so the engine initializes
/// GStreamer once per test binary.
#[cfg(test)]
pub(crate) mod probe_engine {
    use super::{TypeFind, bindings};
    use std::os::raw::c_uint;
    use std::panic::{AssertUnwindSafe, catch_unwind};
    use std::ptr;

    /// The stream under test and the suggestions made to it.
    struct Probe {
        stream: Vec<u8>,
        /// Suggestions in the order the finder made them; a typefinding
        /// function may suggest several times (gst/gsttypefind.h).
        suggestions: Vec<(c_uint, String)>,
    }

    /// The whole request or nothing, and a negative offset measured back
    /// from the end: the real element's peek contract, pinned in the
    /// fuzz harness.
    unsafe extern "C" fn peek(data: bindings::gpointer, offset: i64, size: c_uint) -> *const u8 {
        // SAFETY: `data` is the context's `Probe`.
        let probe = unsafe { &*(data as *const Probe) };
        let Ok(size) = usize::try_from(size) else {
            return ptr::null();
        };
        if size == 0 {
            return ptr::null();
        }
        let Ok(len) = i64::try_from(probe.stream.len()) else {
            return ptr::null();
        };
        let at = if offset < 0 {
            let Some(back) = offset.checked_neg() else {
                return ptr::null();
            };
            if back > len {
                return ptr::null();
            }
            len - back
        } else {
            offset
        };
        let Ok(at) = usize::try_from(at) else {
            return ptr::null();
        };
        match at.checked_add(size) {
            // SAFETY: `at` is in bounds and `end` bounds the read.
            Some(end) if end <= probe.stream.len() => unsafe { probe.stream.as_ptr().add(at) },
            _ => ptr::null(),
        }
    }

    /// Every stream these cases build is a complete file, so its length
    /// is known. A pushed stream states the bytes gathered so far and an
    /// unknown one answers zero (gst/gsttypefindhelper.c); neither is a
    /// complete file, so no probe-level test here models them.
    unsafe extern "C" fn get_length(data: bindings::gpointer) -> u64 {
        // SAFETY: `data` is the context's `Probe`.
        let probe = unsafe { &*(data as *const Probe) };
        probe.stream.len() as u64
    }

    /// Record the suggestion. libgstreamer built the caps from the
    /// variadic tail and owns it across this call, so it is serialized
    /// here: that pins a field's value as well as the media type. Only
    /// the list is borrowed mutably; a peek slice from earlier in the
    /// run may still be alive sharing `stream`.
    unsafe extern "C" fn suggest(
        data: bindings::gpointer,
        probability: c_uint,
        caps: *mut bindings::GstCaps,
    ) {
        // SAFETY: `data` is the context's `Probe`; only the suggestion list is borrowed.
        let suggestions = unsafe { &mut (*(data as *mut Probe)).suggestions };
        // SAFETY: `caps` is valid for this callback.
        let raw = unsafe { bindings::gst_caps_to_string(caps) };
        let caps_str = if raw.is_null() {
            String::new()
        } else {
            // SAFETY: `raw` is NUL-terminated.
            let s = unsafe { std::ffi::CStr::from_ptr(raw) }
                .to_string_lossy()
                .into_owned();
            // SAFETY: `raw` came from GLib and is not used again.
            unsafe { bindings::g_free(raw.cast()) };
            s
        };
        suggestions.push((probability, caps_str));
    }

    /// Run `run` over a fresh context on `probe`, guarded: a panic from
    /// `read` is caught at the boundary of the whole run, the context —
    /// and its `data` pointer into `probe` — is dropped inside it, and
    /// the half-run's suggestions are dropped with the decision to
    /// propagate. The guard is the run's whole contract in one place:
    /// panic or return, `probe` holds nothing a half-run wrote.
    fn guarded<T, F>(probe: &mut Probe, run: F) -> T
    where
        F: for<'a> FnOnce(&'a mut TypeFind) -> T,
    {
        let mut tf = TypeFind(bindings::GstTypeFind {
            peek: Some(peek),
            suggest: Some(suggest),
            data: probe as *mut Probe as bindings::gpointer,
            get_length: Some(get_length),
            _gst_reserved: [ptr::null_mut(); 4],
        });
        match catch_unwind(AssertUnwindSafe(|| run(&mut tf))) {
            Ok(answer) => answer,
            Err(cause) => {
                probe.suggestions.clear();
                std::panic::resume_unwind(cause);
            }
        }
    }

    /// GStreamer initializes once: the suggestion path builds caps
    /// through libgstreamer, so its types register as production's do.
    fn init_gstreamer() {
        use std::sync::Once;
        static INIT: Once = Once::new();
        // SAFETY: `gst_init` may be called once per process.
        INIT.call_once(|| unsafe {
            bindings::gst_init(ptr::null_mut(), ptr::null_mut());
        });
    }

    /// Run `read` with the engine answering from `data`.
    pub(crate) fn probe<T>(data: &[u8], read: impl FnOnce(&mut TypeFind) -> T) -> T {
        init_gstreamer();
        let mut probe = Probe {
            stream: data.to_vec(),
            suggestions: Vec::new(),
        };
        guarded(&mut probe, read)
    }

    /// Run `read` (a whole typefinding function, suggestions included)
    /// against `data` and report its suggestions in the order they were
    /// made: each one's probability and full caps string. An empty
    /// answer means nothing was suggested.
    pub(crate) fn probe_suggestions(
        data: &[u8],
        read: impl FnOnce(&mut TypeFind),
    ) -> Vec<(c_uint, String)> {
        init_gstreamer();
        let mut probe = Probe {
            stream: data.to_vec(),
            suggestions: Vec::new(),
        };
        guarded(&mut probe, read);
        probe.suggestions
    }
}

#[cfg(test)]
mod tests {
    use super::Scanner;
    use super::probe_engine::probe;

    /// A window serves at least the header the finder asked for, and more
    /// when the cache holds more.
    #[test]
    fn a_window_serves_at_least_what_was_asked_for() {
        probe(&[0xAA; 100], |tf| {
            let mut scan = Scanner::new(tf, 0);
            let window = scan.peek_at_least(4).unwrap();
            assert_eq!(window.len(), 100);
            assert_eq!(&window[..4], &[0xAA; 4]);
        });
    }

    /// The tail of a stream is served whole, and a request for more than it
    /// holds is refused rather than answered short.
    #[test]
    fn a_short_tail_is_served_whole() {
        probe(&[0xAA; 10], |tf| {
            let mut scan = Scanner::new(tf, 0);
            assert_eq!(scan.peek_at_least(4).unwrap().len(), 10);
            assert!(scan.peek_at_least(11).is_none());
        });
    }

    /// A cursor that leaves the window behind gets a fresh one at its new
    /// position, however far past the end it walked.
    #[test]
    fn a_window_is_renewed_after_the_cursor_leaves_it() {
        probe(&[0x11; 8192], |tf| {
            let mut scan = Scanner::new(tf, 0);
            let first = scan.peek_at_least(4).unwrap().len();
            scan.consume(first + 7);
            assert_eq!(scan.peek_at_least(4).unwrap().len(), 8192 - first - 7);
        });
    }

    /// An empty request is answered, and one past the end of the stream is
    /// not.
    #[test]
    fn a_request_past_the_end_is_refused() {
        probe(&[0xAA; 10], |tf| {
            let mut scan = Scanner::new(tf, 0);
            assert!(scan.peek_at_least(0).unwrap().is_empty());
            scan.consume(10);
            assert!(scan.peek_at_least(1).is_none());
        });
    }
}
