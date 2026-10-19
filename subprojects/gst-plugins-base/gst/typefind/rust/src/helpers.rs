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
use std::ffi::{CStr, CString};
use std::num::NonZeroU64;
use std::os::raw::{c_char, c_uint};
use std::ptr;

#[repr(transparent)]
pub struct TypeFind(pub(crate) bindings::GstTypeFind);

impl TypeFind {
    pub fn peek_var(&mut self, offset: i64, size: u32) -> Option<&[u8]> {
        assert!(size > 0);

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
    /// Per the C API: `0` means "not available"; positive values are known lengths.
    pub fn length(&mut self) -> Option<NonZeroU64> {
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

    /// Peek N bytes from offset 0 and compare with a magic array.
    /// Returns `true` only when enough data is available and matches.
    pub fn starts_with<const N: usize>(&mut self, magic: &[u8; N]) -> bool {
        matches!(self.peek::<N>(0), Some(data) if data == magic)
    }
}

/// Cached window size of `Scanner`, in bytes.
const WINDOW: usize = 4096;

/// Zero-copy windowed cursor over the typefind stream, in the spirit of
/// `std::io::BufRead`: `peek()` serves bytes from a cached window and only
/// queries the engine again when the window cannot cover the request;
/// `consume()` advances. The scanner owns `&mut TypeFind` for the walk, and
/// a cached window is only read until the next query replaces it.
pub struct Scanner<'a> {
    tf: &'a mut TypeFind,
    base: *const u8, // cached window
    offset: i64,     // stream offset of `base`
    len: usize,      // window length
    pos: usize,      // cursor within the window
}

impl<'a> Scanner<'a> {
    pub fn new(tf: &'a mut TypeFind, offset: i64) -> Self {
        Self {
            tf,
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
        // Sound: the slice lives no longer than the exclusive borrow of the
        // scanner, and the window it points into is only read until the next
        // query replaces it.
        Some(unsafe { std::slice::from_raw_parts(self.base.add(self.pos), min) })
    }

    /// Declare the next `n` bytes consumed. May run past the window end; the
    /// next `peek` queries the engine at the right offset.
    pub fn consume(&mut self, n: usize) {
        self.pos += n;
    }

    /// Current stream position of the cursor.
    pub fn offset(&self) -> i64 {
        self.offset + self.pos as i64
    }

    /// Query the engine for up to `WINDOW` bytes at `offset`, falling back
    /// to whatever the stream has left (at least `min`) for short tails.
    fn refill(&mut self, offset: i64, min: usize) -> Option<(*const u8, usize)> {
        let chunk = WINDOW.max(min) as u32;
        let base = unsafe { bindings::gst_type_find_peek(&mut self.tf.0, offset, chunk) };
        if !base.is_null() {
            return Some((base, chunk as usize));
        }
        // Short tail: the stream ends before a full window. An unknown
        // length lands here and asks for at least `min` bytes.
        let left = self
            .tf
            .length()
            .map_or(0, NonZeroU64::get)
            .saturating_sub(offset as u64);
        let len = left.clamp(min as u64, chunk as u64) as u32;
        let base = unsafe { bindings::gst_type_find_peek(&mut self.tf.0, offset, len) };
        (!base.is_null()).then_some((base, len as usize))
    }
}

/// Registered typefind function ABI, C signature
/// `void (*) (GstTypeFind *tf, gpointer user_data)`. TypeFind is a
/// repr(transparent) wrapper, so `&mut TypeFind` has pointer ABI.
pub type TypeFindFunc = extern "C" fn(&mut TypeFind, gpointer);

pub(crate) struct Feature(
    pub(crate) &'static str,         // feature name
    pub(crate) c_uint,               // rank
    pub(crate) Option<TypeFindFunc>, // typefind fn
    pub(crate) Option<&'static str>, // extensions
    pub(crate) Option<&'static str>, // caps string
);

/// 16-bit little-endian value at the start of `d`. The caller's peek
/// guarantees the length.
pub(crate) fn u16le(d: &[u8]) -> u16 {
    u16::from_le_bytes([d[0], d[1]])
}

/// 32-bit little-endian value at the start of `d`. The caller's peek
/// guarantees the length.
pub(crate) fn u32le(d: &[u8]) -> u32 {
    u32::from_le_bytes([d[0], d[1], d[2], d[3]])
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
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct TypeFindProbability(c_uint);

impl TypeFindProbability {
    pub const NONE: Self = Self(bindings::GST_TYPE_FIND_NONE);
    pub const MINIMUM: Self = Self(bindings::GST_TYPE_FIND_MINIMUM);
    pub const POSSIBLE: Self = Self(bindings::GST_TYPE_FIND_POSSIBLE);
    pub const LIKELY: Self = Self(bindings::GST_TYPE_FIND_LIKELY);
    pub const NEARLY_CERTAIN: Self = Self(bindings::GST_TYPE_FIND_NEARLY_CERTAIN);
    pub const MAXIMUM: Self = Self(bindings::GST_TYPE_FIND_MAXIMUM);

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

/// One row of the registration table (typefindfunctions.rs::FEATURES), in
/// gst_type_find_register() argument order. Strings stay plain Rust data;
/// NULL-terminated conversion happens in gst_typefindfunctions_rs_register().
/// Caps default to the feature name; verify each row against the C caps string.
///
/// - `typefinder!("image/bmp", PRIMARY, bmp_type_find, "bmp")`
///   extensions "bmp", caps = feature name (default)
/// - `typefinder!("subtitle/x-kate", MARGINAL, none, "gsm")`
///   NULL func, NULL caps; extension-only registration
/// - `typefinder!("image/tiff", PRIMARY, tiff_type_find, "tif,tiff", "image/tiff, endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }")`
///   caps differing from the feature name
/// - `typefinder!("image/x-icon", MARGINAL, windows_icon_type_find, none, none)`
///   NULL extensions and NULL caps
///
/// Ranks: PRIMARY/SECONDARY/MARGINAL (or offsets like `PRIMARY+103`).
/// `none` = C NULL argument. 3-arg form defaults to caps = name,
/// extensions = None.
macro_rules! typefinder {
    ($name:expr, $rank:expr, $func:expr) => {
        Feature($name, $rank, opt!($func), None, Some($name))
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr) => {
        Feature($name, $rank, opt!($func), Some($exts), Some($name))
    };
    // Ahead of the arms that capture `$exts` or `$caps`: `none` parses as an
    // expression there, and a captured expression no longer matches the
    // literal `none` pattern.
    ($name:expr, $rank:expr, $func:expr, none, none) => {
        Feature($name, $rank, opt!($func), None, None)
    };
    ($name:expr, $rank:expr, $func:expr, none, $caps:expr) => {
        Feature($name, $rank, opt!($func), None, opt!($caps))
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr, $caps:expr) => {
        Feature($name, $rank, opt!($func), opt!($exts), opt!($caps))
    };
}

/// Converts `data` from `from_codeset` to UTF-8, replacing unconvertible bytes with '*'.
/// Strips any UTF-8 BOM that the converter passes through. Returns None on converter error.
pub(crate) fn convert_to_utf8(data: &[u8], from_codeset: &str) -> Option<Vec<u8>> {
    let from = CString::new(from_codeset).ok()?;
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

// Alias for callers who want the short name.
pub(crate) use TypeFindProbability as Prob;

// Re-export macros and items needed by typefindfunctions.rs.
pub(crate) use {map_type_name, map_value, opt, suggest, typefinder};
