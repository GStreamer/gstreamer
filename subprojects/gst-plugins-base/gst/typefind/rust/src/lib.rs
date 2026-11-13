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

//! Rust implementations of GStreamer typefind functions.

mod audio;
mod av1;
mod bindings;
mod containers;
mod h26x;
mod helpers;
mod mpeg;
mod scan;
mod startcode;
mod subtitles;
mod typefindfunctions;
// Vendored shared-MIME reader (see the module docs). The finder uses only
// the content-sniffing entry point; the rest of the API is kept whole from
// upstream xdg-mime for fidelity and is covered by this module's tests, so
// it is not dead in intent — only from the non-test build's single call site.
#[allow(dead_code)]
mod xdg_mime;
mod xml;
