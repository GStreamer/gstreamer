// GStreamer
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

#![no_main]

//! Every registered finder, one per fuzz case. The leading byte picks which.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let Some((&selector, rest)) = data.split_first() else {
        return;
    };
    if let Some(finder) = gst_typefind_fuzz::pick(selector, None) {
        gst_typefind_fuzz::run(finder, rest);
    }
});
