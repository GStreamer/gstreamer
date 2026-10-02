// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Typefinder registration table and entry point.

#[allow(unused, non_camel_case_types, non_upper_case_globals, non_snake_case)]
use crate::bindings;
use bindings::{
    gboolean, gst_caps_from_string, gst_type_find_register, GstPlugin, GST_TYPE_FIND_MAXIMUM,
};

use std::ffi::{CStr, CString};
use std::ptr;

use crate::bindings::gpointer;
use crate::helpers::{opt, read_u16_le, read_u32_le, suggest, typefinder, Feature, TypeFind};
#[allow(unused_imports)]
use bindings::{
    GST_RANK_MARGINAL as MARGINAL, GST_RANK_PRIMARY as PRIMARY, GST_RANK_SECONDARY as SECONDARY,
};

// BMP — image/bmp
// Magic "BM" @0, DIB header validation at offsets 14–28/32.
extern "C" fn bmp_type_find(tf: &mut TypeFind, _private: gpointer) {
    const TEN_MIB: u32 = 10 * 1024 * 1024;
    const MAX_DIM: u32 = 0xFFFFF;

    let Some(data) = tf.peek::<32>(0) else { return };

    if data[0] != b'B' || data[1] != b'M' || data[6..10].iter().any(|&b| b != 0) {
        return;
    }
    if read_u32_le(&data[10..]).unwrap() > TEN_MIB {
        return;
    }

    let (w, h, planes, bpp) = match read_u32_le(&data[14..]).unwrap() {
        0x0C => {
            // BITMAPCOREHEADER — w,h as u16.
            if let (Some(w), Some(h), Some(planes), Some(bpp)) = (
                read_u16_le(&data[18..]),
                read_u16_le(&data[20..]),
                read_u16_le(&data[22..]),
                read_u16_le(&data[24..]),
            ) {
                (u32::from(w), u32::from(h), planes, bpp)
            } else {
                return;
            }
        }
        40 | 64 | 108 | 124 | 0xF0 => {
            // BITMAPINFOHEADER V3–V4 — w,h as u32.
            if let (Some(w), Some(h), Some(planes), Some(bpp)) = (
                read_u32_le(&data[18..]),
                read_u32_le(&data[22..]),
                read_u16_le(&data[26..]),
                read_u16_le(&data[28..]),
            ) {
                (w, h, planes, bpp)
            } else {
                return;
            }
        }
        _ => return,
    };

    if w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM || planes != 1 {
        return;
    }
    if !matches!(bpp, 1 | 4 | 8 | 16 | 24 | 32) {
        return;
    }

    suggest!(tf, GST_TYPE_FIND_MAXIMUM,
             c"image/bmp",
             [ (c"width", i32 : w as i32),
               (c"height", i32 : h as i32),
               (c"bpp", i32 : bpp as i32), ]);
}

/// Registration table: one entry per typefinder.
/// The `typefinder!` macro expands to a `Feature` tuple.
///
/// Signature: `typefinder!("mime/type", RANK, func_name, "exts"?)`
/// - `"mime/type"`  – feature name and default caps
/// - `RANK`         – PRIMARY, SECONDARY, MARGINAL (or offset like PRIMARY+103)
/// - `func_name`    – the typefind function, or `none` for extension-only registration
/// - `"exts"`       – comma-separated extensions string (optional)
///
/// To add a row after implementing a typefinder:
///   typefinder!("audio/x-musepack", PRIMARY, musepack_type_find, "mpc")
const FEATURES: &[Feature] = &[typefinder!("image/bmp", PRIMARY, bmp_type_find, "bmp")];

/// Register all Rust-implemented typefinders with the GStreamer plugin system.
#[no_mangle]
pub extern "C" fn gst_typefindfunctions_rs_register(plugin: &mut GstPlugin) -> gboolean {
    let cs = |s: &str| CString::new(s).expect("NULL in registration table string");

    for &Feature(name, rank, func, extensions, caps_str) in FEATURES {
        let name = cs(name);
        let extensions = extensions.map(cs);
        let caps_str = caps_str.map(cs);

        // NULL on a typo'd caps string, as in C.
        let caps = caps_str.map_or(ptr::null_mut(), |s| unsafe {
            gst_caps_from_string(s.as_ptr())
        });
        // Return value ignored, as in C: only a NULL name fails.
        unsafe {
            gst_type_find_register(
                plugin,
                name.as_ptr(),
                rank,
                func,
                extensions.as_deref().map_or(ptr::null(), CStr::as_ptr),
                caps,
                ptr::null_mut(),
                None,
            );
        }
    }
    true as gboolean
}
