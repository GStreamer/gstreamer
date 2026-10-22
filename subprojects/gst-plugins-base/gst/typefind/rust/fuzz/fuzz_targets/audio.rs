// GStreamer
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

#![no_main]

//! The audio finders only, one per fuzz case. Narrower than `all`, so the
//! coverage signal concentrates on the frame walkers, which is where the
//! arithmetic lives.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let Some((&selector, rest)) = data.split_first() else {
        return;
    };
    if let Some(finder) = gst_typefind_fuzz::pick(selector, Some("audio/")) {
        gst_typefind_fuzz::run(finder, rest);
    }
});
