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
mod startcode;
mod subtitles;
mod typefindfunctions;
// The shared MIME database reader is not wired into plugin registration
// yet; it builds and is exercised by its own unit tests.
#[allow(dead_code)]
mod xdg_mime;
mod xml;
