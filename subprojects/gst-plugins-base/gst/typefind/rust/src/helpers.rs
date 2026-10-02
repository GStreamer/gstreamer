// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use crate::bindings::{self, gpointer};
use std::os::raw::c_uint;

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

    pub fn length(&mut self) -> u64 {
        unsafe { bindings::gst_type_find_get_length(&mut self.0) }
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

/// Read a 16-bit little-endian value from a byte slice. Returns None if the
/// slice is shorter than 2 bytes.
pub(crate) fn read_u16_le(d: &[u8]) -> Option<u16> {
    match d {
        [a, b, ..] => Some(u16::from_le_bytes([*a, *b])),
        _ => None,
    }
}

/// Read a 32-bit little-endian value from a byte slice. Returns None if the
/// slice is shorter than 4 bytes.
pub(crate) fn read_u32_le(d: &[u8]) -> Option<u32> {
    match d {
        [a, b, c, d, ..] => Some(u32::from_le_bytes([*a, *b, *c, *d])),
        _ => None,
    }
}

/// Allows to enforce a specific type for the argument.
///
/// Helper function for the macros below.
pub(crate) fn type_check<T: ?Sized>(_v: &T) {}

/// Maps a type name literal to the matching fundamental `GType` id.
macro_rules! map_type_name {
    (i32) => {
        bindings::G_TYPE_INT
    };
    (str) => {
        bindings::G_TYPE_STRING
    };
    (bool) => {
        bindings::G_TYPE_BOOLEAN
    };
}

/// Maps a type name literal and value to the value used in the call.
macro_rules! map_value {
    (i32, $val:expr) => {{
        $crate::helpers::type_check::<i32>(&$val);
        $val
    }};
    (str, $val:expr) => {{
        $crate::helpers::type_check::<std::ffi::CStr>(&$val);
        $val
    }
    .as_ptr()};
    (bool, $val:expr) => {{
        $crate::helpers::type_check::<bool>(&$val);
        if $val {
            1i32
        } else {
            0i32
        }
    }};
}

/// Suggest a media type and optional caps fields to the typefind engine.
///
/// Usage:
/// ```ignore
/// // Type only (no caps fields)
/// suggest!(tf, GST_TYPE_FIND_MAXIMUM, c"image/bmp");
///
/// // Type with caps fields: `suggest!(tf, prob, "mime", [(key, type : value), ...])`
/// // Field types: `i32`, `str`, `bool` — compile-time checked against GType
/// suggest!(tf, GST_TYPE_FIND_MAXIMUM, c"image/bmp",
///     [ (c"width", i32 : 640),
///       (c"height", i32 : 480) ]);
/// ```
macro_rules! suggest {
    ($tf:expr, $prob:expr, $name:expr $(,)?) => {
        $crate::helpers::suggest!($tf, $prob, $name, []);
    };
    ($tf:expr, $prob:expr, $name:expr, []) => {
        unsafe {
            bindings::gst_type_find_suggest_simple(
                &mut $tf.0,
                $prob,
                {
                    $crate::helpers::type_check::<std::ffi::CStr>(&$name);
                    $name
                }.as_ptr(),
                std::ptr::null::<::std::os::raw::c_char>(),
            );
        }
    };
    ($tf:expr, $prob:expr, $name:expr, [ $( ($key:expr, $type:ident : $val:expr) , )*] $(,)?) => {
        unsafe {
            bindings::gst_type_find_suggest_simple(
                &mut $tf.0,
                $prob,
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
/// Caps default to the feature name, which is only correct when the C
/// registration uses it as caps; musepack, id3v1/id3v2 and tiff do not, so
/// verify each row against the C caps string.
///
/// - `typefinder!("image/bmp", PRIMARY, bmp_type_find, "bmp")`
///   extensions "bmp", caps = feature name
/// - `typefinder!("subtitle/x-kate", MARGINAL, kate_type_find, none, none)`
///   NULL extensions, NULL caps; a both-NULL row needs all five arguments
/// - `typefinder!("image/tiff", PRIMARY, tiff_type_find, "tif,tiff", "image/tiff, endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }")`
///   caps differing from the feature name
///
/// `none` stands for a C NULL argument, also for the function (extension-only
/// registrations like gsm). Ranks are the bare PRIMARY/SECONDARY/MARGINAL
/// aliases, offset like C where needed (`PRIMARY + 103`).
macro_rules! typefinder {
    ($name:expr, $rank:expr, $func:expr) => {
        typefinder!($name, $rank, $func, none)
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr) => {
        typefinder!($name, $rank, $func, $exts, $name)
    };
    ($name:expr, $rank:expr, $func:expr, $exts:expr, $caps:expr) => {
        Feature($name, $rank, opt!($func), opt!($exts), opt!($caps))
    };
}

pub(crate) use {map_type_name, map_value, opt, suggest, typefinder};
