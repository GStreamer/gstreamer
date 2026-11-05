# xdg-mime

Reader for the shared MIME database, used by the `xdg-mime` typefinder.

Derived from the `xdg-mime` crate, version 0.4.0
(<https://github.com/ebassi/xdg-mime-rs>, tag `0.4.0`, commit
`1c1efabc504ca3e00170cba78072d1864209e3f6`), Copyright (c) Emmanuele Bassi,
Apache-2.0. See `LICENSE-APACHE`.

The glob and magic matchers and the guessing pipeline follow upstream. Its
five dependencies are replaced by std or GLib equivalents so the crate builds
offline; `mod.rs` documents the mapping and the features left out.
