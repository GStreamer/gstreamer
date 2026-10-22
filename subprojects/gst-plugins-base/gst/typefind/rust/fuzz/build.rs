// GStreamer
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

// The finders call a handful of real library functions that the harness does
// not replace: the GLib charset helpers used by the text finders, and the
// AudioSpecificConfig readers used by AAC, which live in pbutils. Those get
// linked for real; the three GstTypeFind entry points are interposed by this
// crate instead.

use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    for pkg in ["glib-2.0", "gstreamer-pbutils-1.0"] {
        let out = Command::new("pkg-config")
            .args(["--libs", pkg])
            .output()
            .expect("`pkg-config` is needed to link the libraries the finders call");
        assert!(
            out.status.success(),
            "`pkg-config --libs {pkg}` failed: {}",
            String::from_utf8_lossy(&out.stderr)
        );
        let flags = String::from_utf8(out.stdout).expect("pkg-config output is not UTF-8");
        let mut words = flags.split_whitespace().peekable();
        while let Some(word) = words.next() {
            if let Some(path) = word.strip_prefix("-L") {
                println!("cargo:rustc-link-search=native={path}");
            } else if let Some(lib) = word.strip_prefix("-l") {
                println!("cargo:rustc-link-lib=dylib={lib}");
            }
        }
    }
}
