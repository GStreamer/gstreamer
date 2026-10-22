// GStreamer
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Drive the registered finders from a byte slice, with no GStreamer at run
//! time. Meson never builds this crate.
//!
//! The finders reach the engine through exactly three entry points: peek, get
//! length, and suggest. Defining those here and linking the crate as an rlib
//! replaces the engine with a byte slice, so a fuzz target calls the same
//! function pointers the plugin registers and nothing else differs.
//!
//! The registry is harvested rather than duplicated: `registry()` runs the
//! crate's own registration pass with `gst_type_find_register` interposed, so
//! every factory in the table is covered and there is no second list to keep
//! in step.

// Linked for its finders, which are reached through the C entry points below
// rather than by path, so the crate has to be named to stay on the link line.
extern crate gst_typefind_rs;

use std::cell::RefCell;
use std::ffi::{CStr, c_char, c_int, c_uint, c_void};
use std::ptr;
use std::sync::OnceLock;

/// One registered finder, as the registration pass handed it over.
pub struct Finder {
    pub name: String,
    pub extensions: String,
    func: extern "C" fn(*mut c_void, *mut c_void),
    /// The `data` the registry passed; the shared start_with finder reads the
    /// row back through it. Stored as `usize` so `Finder` stays `Send + Sync`
    /// for the `OnceLock`.
    data: usize,
}

/// What one finder suggested: probability and media type, in call order.
pub type Suggests = Vec<(u32, String)>;

#[derive(Default)]
struct Ctx {
    data: Vec<u8>,
    suggests: Vec<(u32, String)>,
}

thread_local! {
    static CTX: RefCell<Ctx> = RefCell::new(Ctx::default());
    static HARVEST: RefCell<Vec<Finder>> = const { RefCell::new(Vec::new()) };
}

// --- the three entry points the finders call -------------------------------

/// Answer with the whole request or with nothing, and treat a negative offset
/// as a distance back from the end. Both are the real helper's contract, and
/// the finders depend on the all-or-nothing half of it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_type_find_peek(
    _find: *mut c_void,
    offset: i64,
    size: c_uint,
) -> *const u8 {
    CTX.with(|cell| {
        let ctx = cell.borrow();
        let size = usize::try_from(size).ok()?;
        if size == 0 {
            return Some(ptr::null());
        }
        let len = i64::try_from(ctx.data.len()).ok()?;
        let at = if offset < 0 {
            let back = offset.checked_neg()?;
            if back > len {
                return Some(ptr::null());
            }
            len - back
        } else {
            offset
        };
        let at = usize::try_from(at).ok()?;
        let end = at.checked_add(size)?;
        if end > ctx.data.len() {
            return Some(ptr::null());
        }
        Some(unsafe { ctx.data.as_ptr().add(at) })
    })
    .unwrap_or(ptr::null())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_type_find_get_length(_find: *mut c_void) -> u64 {
    CTX.with(|cell| cell.borrow().data.len() as u64)
}

/// The oracle. GStreamer answers an out-of-range probability with a warning a
/// fuzzer walks straight past, so here it is a failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_type_find_suggest_simple(
    _find: *mut c_void,
    probability: c_uint,
    media_type: *const c_char,
    _fieldname: *const c_char,
    _: ...
) {
    let name = media_type_name(media_type);
    assert!(
        probability <= 100,
        "{name} suggested probability {probability}, above the maximum of 100"
    );
    assert!(
        name.contains('/'),
        "suggested media type {name:?} is not a type/subtype pair"
    );
    CTX.with(|cell| cell.borrow_mut().suggests.push((probability, name)));
}

fn media_type_name(media_type: *const c_char) -> String {
    assert!(!media_type.is_null(), "suggested a NULL media type");
    unsafe { CStr::from_ptr(media_type) }
        .to_string_lossy()
        .into_owned()
}

// --- the registration pass, intercepted ------------------------------------

#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_type_find_register(
    _plugin: *mut c_void,
    name: *const c_char,
    _rank: c_uint,
    func: Option<extern "C" fn(*mut c_void, *mut c_void)>,
    extensions: *const c_char,
    _caps: *mut c_void,
    data: *mut c_void,
    _notify: *mut c_void,
) -> c_int {
    let Some(func) = func else { return 1 };
    let text = |p: *const c_char| {
        p.is_null()
            .then(String::new)
            .unwrap_or_else(|| unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned())
    };
    HARVEST.with(|cell| {
        cell.borrow_mut().push(Finder {
            name: text(name),
            extensions: text(extensions),
            func,
            data: data as usize,
        })
    });
    1
}

/// Caps are only carried through registration here, but a string that would
/// not parse is worth failing on, because the real call answers it with NULL
/// and the finder silently loses its default caps.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_caps_from_string(caps: *const c_char) -> *mut c_void {
    let text = unsafe { CStr::from_ptr(caps) }
        .to_string_lossy()
        .into_owned();
    assert!(
        text.contains('/'),
        "registered caps {text:?} would not parse and would be dropped"
    );
    // A non-NULL handle is all the registration path asks for.
    1 as *mut c_void
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn gst_caps_unref(_caps: *mut c_void) {}

unsafe extern "C" {
    fn gst_typefindfunctions_rs_register(plugin: *mut c_void) -> c_int;
}

/// Every finder the crate registers, in registration order.
pub fn registry() -> &'static [Finder] {
    static REGISTRY: OnceLock<Vec<Finder>> = OnceLock::new();
    REGISTRY.get_or_init(|| {
        // The plugin pointer only travels through to `gst_type_find_register`,
        // which is interposed above and ignores it.
        static PLUGIN: [u8; 8] = [0; 8];
        let got = unsafe { gst_typefindfunctions_rs_register(&PLUGIN as *const u8 as *mut c_void) };
        assert_eq!(got, 1, "the registration pass refused to run");
        HARVEST.with(|cell| std::mem::take(&mut *cell.borrow_mut()))
    })
}

/// The finder a selector byte names, or `None` when it names one that
/// `needle` excludes. Every target indexes the same space, so one seeded
/// corpus feeds all of them and the count a seeder needs is just the length
/// of the registry.
pub fn pick(selector: u8, needle: Option<&str>) -> Option<&'static Finder> {
    let finder = &registry()[selector as usize % registry().len()];
    match needle {
        Some(needle) => finder.name.contains(needle).then_some(finder),
        None => Some(finder),
    }
}

/// The finders whose registered type or extension matches `needle`.
pub fn matching(needle: &str) -> Vec<&'static Finder> {
    registry()
        .iter()
        .filter(|f| {
            f.name.contains(needle)
                || f.extensions
                    .split(',')
                    .any(|ext| ext == needle || f.name.contains(ext))
        })
        .collect()
}

// --- driving one finder ----------------------------------------------------

/// Run `finder` over `data` and report what it suggested.
pub fn run(finder: &Finder, data: &[u8]) -> Suggests {
    CTX.with(|cell| {
        let mut ctx = cell.borrow_mut();
        ctx.data = data.to_vec();
        ctx.suggests.clear();
    });
    // The finder takes a `&mut GstTypeFind`; the crate's own wrapper is
    // transparent over the opaque type, so any non-NULL pointer is a fine
    // stand-in while every read goes through the entry points above.
    let mut token: usize = 0x5f7466;
    (finder.func)(&raw mut token as *mut c_void, finder.data as *mut c_void);
    CTX.with(|cell| std::mem::take(&mut cell.borrow_mut().suggests))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A harness that quietly does nothing is worse than no harness at all, and
    /// an empty harvest is how that would look.
    #[test]
    fn the_registration_pass_yields_the_finders() {
        assert!(
            registry().len() > 100,
            "harvested only {} finders",
            registry().len()
        );
    }

    /// Four mono frames, four subbands, four blocks, bitpool two: four octets
    /// of header, two of scale factors and one of data, so seven octets each.
    fn sbc() -> Vec<u8> {
        let frame = [0x9C, 0xC0, 2, 0, 0, 0, 0];
        frame.repeat(4)
    }

    /// End to end: bytes in, through the interposed peek, out through the
    /// interposed suggest. If either interposition failed to take, this is the
    /// test that says so.
    #[test]
    fn a_real_stream_suggests_its_type() {
        let finder = matching("sbc")
            .into_iter()
            .find(|f| f.name == "audio/x-sbc")
            .expect("no audio/x-sbc finder registered");
        let got = run(finder, &sbc());
        assert_eq!(got, vec![(50, "audio/x-sbc".to_string())]);
    }

    /// The shared start_with finder reads its pattern and name back through
    /// the registration `data` pointer. Running it with a NULL pointer crashed
    /// on the first dereference, so this test pins the hand-over.
    #[test]
    fn a_start_with_finder_receives_its_data() {
        let finder = matching("shockwave-flash")
            .into_iter()
            .find(|f| f.name == "application/x-shockwave-flash")
            .expect("no shockwave-flash finder registered");
        let got = run(finder, b"FWS\x0a\0\0\0");
        assert_eq!(
            got,
            vec![(100, "application/x-shockwave-flash".to_string())]
        );
    }

    /// `cargo +nightly test --release -p gst-typefind-fuzz -- --ignored` prints
    /// the table, so the seeder never has to guess how many finders there are.
    #[test]
    #[ignore]
    fn print_the_registry() {
        for (i, f) in registry().iter().enumerate() {
            println!("{i}\t{}\t{}", f.name, f.extensions);
        }
    }

    /// A target whose selector space is dead looks identical to a clean run
    /// except for the coverage count, so pin it here rather than trust a
    /// fuzzer's silence.
    #[test]
    fn no_selector_is_dead() {
        let reached = (0..registry().len())
            .filter(|i| pick(*i as u8, None).is_some())
            .count();
        assert_eq!(reached, registry().len(), "some selectors reach nothing");
    }

    #[test]
    fn the_audio_filter_keeps_the_audio_finders() {
        let want = registry()
            .iter()
            .filter(|f| f.name.contains("audio/"))
            .count();
        let kept = (0..registry().len())
            .filter(|i| pick(*i as u8, Some("audio/")).is_some())
            .count();
        assert!(
            want > 10,
            "only {want} audio finders, has the registry changed?"
        );
        assert_eq!(kept, want, "the audio filter kept the wrong set");
    }

    /// The peek contract the finders lean on: the whole request or nothing, and
    /// a negative offset measured back from the end.
    #[test]
    fn peek_answers_with_the_whole_request_or_nothing() {
        CTX.with(|cell| cell.borrow_mut().data = vec![0u8; 16]);
        let at = |off: i64, size: c_uint| unsafe { gst_type_find_peek(ptr::null_mut(), off, size) };
        assert!(!at(0, 16).is_null());
        assert!(at(0, 17).is_null());
        assert!(at(8, 9).is_null());
        assert!(!at(-4, 4).is_null());
        assert!(at(-17, 1).is_null());
        assert!(at(0, 0).is_null());
        CTX.with(|cell| cell.borrow_mut().data.clear());
    }
}
