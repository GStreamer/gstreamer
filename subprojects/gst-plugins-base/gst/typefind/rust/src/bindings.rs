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

//! Hand-written FFI declarations for the GStreamer typefind API.
//!
//! Deliberately minimal — only what this crate uses. Source of truth is
//! gst/gsttypefind.h plus the glib/GObject constants below; extend by hand
//! if the used API surface grows. No bindgen, no build script: bindgen needs
//! libclang at build time, which complicates offline and cross builds, and it
//! would generate the whole headers for the handful of items used here.

#![allow(dead_code, non_camel_case_types, non_upper_case_globals)]

use std::os::raw::{c_char, c_int, c_uint, c_void};

use crate::helpers::TypeFindFunc;

/// Opaque typefind context
#[repr(C)]
pub struct GstTypeFind {
    _private: [u8; 0],
}

/// Opaque plugin object
#[repr(C)]
pub struct GstPlugin {
    _private: [u8; 0],
}

/// Opaque caps object
#[repr(C)]
pub struct GstCaps {
    _private: [u8; 0],
}

pub type gpointer = *mut c_void;
pub type gboolean = c_int;
/// GDestroyNotify callback, registered as NULL in this crate.
pub type GDestroyNotify = Option<unsafe extern "C" fn(gpointer)>;
/// gsize on all supported platforms; GType values are passed as
/// gsize-sized varargs to gst_type_find_suggest_simple().
pub type GType = usize;

/// GstTypeFindProbability (gst/gsttypefind.h)
pub const GST_TYPE_FIND_NONE: c_uint = 0;
pub const GST_TYPE_FIND_MINIMUM: c_uint = 1;
pub const GST_TYPE_FIND_POSSIBLE: c_uint = 50;
pub const GST_TYPE_FIND_LIKELY: c_uint = 80;
pub const GST_TYPE_FIND_NEARLY_CERTAIN: c_uint = 99;
pub const GST_TYPE_FIND_MAXIMUM: c_uint = 100;

/// glib byte-order marks (glib/gtypes.h).
pub const G_LITTLE_ENDIAN: c_uint = 1234;
pub const G_BIG_ENDIAN: c_uint = 4321;

/// GstRank (gst/gstpluginfeature.h). Frozen ABI values.
pub const GST_RANK_MARGINAL: c_uint = 64;
pub const GST_RANK_SECONDARY: c_uint = 128;
pub const GST_RANK_PRIMARY: c_uint = 256;

/// Fundamental GTypes (gobject/gtype.h, G_TYPE_MAKE_FUNDAMENTAL). The
/// fundamental type ids are frozen GObject ABI: existing values never
/// change.
pub const G_TYPE_BOOLEAN: GType = 20;
pub const G_TYPE_INT: GType = 24;
pub const G_TYPE_STRING: GType = 64;

unsafe extern "C" {
    pub fn gst_type_find_peek(find: *mut GstTypeFind, offset: i64, size: c_uint) -> *const u8;

    pub fn gst_type_find_get_length(find: *mut GstTypeFind) -> u64;

    pub fn gst_type_find_suggest_simple(
        find: *mut GstTypeFind,
        probability: c_uint,
        media_type: *const c_char,
        fieldname: *const c_char,
        ...
    );

    // The callback takes &mut TypeFind, whose only field is the opaque
    // GstTypeFind, so only pointers cross the boundary and the lint is a
    // false positive.
    #[allow(improper_ctypes)]
    pub fn gst_type_find_register(
        plugin: *mut GstPlugin,
        name: *const c_char,
        rank: c_uint,
        func: Option<TypeFindFunc>,
        extensions: *const c_char,
        caps: *mut GstCaps,
        data: gpointer,
        data_notify: GDestroyNotify,
    ) -> gboolean;

    pub fn gst_caps_from_string(string: *const c_char) -> *mut GstCaps;
    pub fn gst_caps_unref(caps: *mut GstCaps);
}
