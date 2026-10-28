// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Probes for container formats that say what they are by the shape of their
//! top-level boxes rather than by a single magic value. Each probe decides what
//! the stream is; the typefinders only render the decision.

use crate::helpers::{Prob, TypeFind, u32be};
use std::ffi::CStr;

/// A four octet box type or brand, as ISOBMFF stores them.
type Tag = [u8; 4];

/// ISOBMFF 4.2: a box opens with a 32-bit size and a four octet type, and a
/// size of 1 means the real size is the 64-bit `largesize` after the type.
const BOX_MIN: u64 = 8;

/// Ceiling on how much of a file-type box is read, used when the stream has no
/// known length and there is nothing else to clamp against. The size a header
/// declares is attacker-chosen, so it cannot be honoured on its own.
const FTYP_SCAN_MAX: u64 = 65536;

fn tag_at(head: &[u8], at: usize, tags: &[Tag]) -> bool {
    tags.iter().any(|t| head[at..at + 4] == *t)
}

// =============================================================================
// video/quicktime
// =============================================================================

/// CMAF segmentation and media brands, registered in ISO 23009-1.
const CMAF_BRANDS: &[Tag] = &[
    *b"cmf2", *b"cmfc", *b"cfsd", *b"cfhd", *b"chdf", *b"caac", *b"caaa", *b"ccea", *b"chhd",
    *b"chh1", *b"cud8", *b"cud1", *b"chd1", *b"clg1",
];

/// Brands for a plain base media file format presentation, including the MP4v2
/// and AVC brands grouped with them.
const ISO_BRANDS: &[Tag] = &[
    *b"isom", *b"avc1", *b"iso3", *b"iso4", *b"iso5", *b"iso6", *b"iso7", *b"iso8", *b"iso9",
    *b"isoa", *b"isob", *b"mp42", *b"mp41",
];

/// Brands reported as a segmented presentation. `avc3` is an AVC codec brand
/// rather than a segmentation brand; the grouping is inherited, not semantic.
const ISO_FRAGMENTED_BRANDS: &[Tag] = &[*b"isml", *b"dash", *b"hlsf", *b"avc3"];

const CCFF_BRANDS: &[Tag] = &[*b"ccff"];

/// ISO 23008-12, which borrows the base media file format box layer.
const HEIF_BRANDS: &[Tag] = &[*b"mif1"];

/// Top-level boxes defined by the base media file format and its segment and
/// DASH extensions, so seeing one says nothing about which dialect it is.
const COMMON_BOXES: &[Tag] = &[
    *b"moov", *b"mdat", *b"ftyp", *b"free", *b"uuid", *b"udta", *b"styp", *b"sidx", *b"ssix",
    *b"prft", *b"emsg", *b"moof", *b"mfra", *b"pdin", *b"meta", *b"skip",
];

/// Top-level boxes the base media file format does not define, so one of these
/// settles it for QuickTime.
const QT_ONLY_BOXES: &[Tag] = &[*b"pnot", *b"PICT", *b"wide", *b"prfl"];

/// The dialect a major brand may name, tried in this order.
const MAJOR_DIALECTS: [(&[Tag], &CStr); 5] = [
    (CMAF_BRANDS, c"cmaf"),
    (ISO_BRANDS, c"iso"),
    (ISO_FRAGMENTED_BRANDS, c"iso-fragmented"),
    (CCFF_BRANDS, c"ccff"),
    (HEIF_BRANDS, c"heif"),
];

/// The dialect a compatible brand may name. `ccff` is not here: it says nothing
/// on its own unless it is the major brand.
const COMPAT_DIALECTS: [(&[Tag], &CStr); 4] = [
    (CMAF_BRANDS, c"cmaf"),
    (ISO_BRANDS, c"iso"),
    (ISO_FRAGMENTED_BRANDS, c"iso-fragmented"),
    (HEIF_BRANDS, c"heif"),
];

fn dialect(brand: &[u8], groups: &[(&[Tag], &'static CStr)]) -> Option<&'static CStr> {
    groups
        .iter()
        .find(|(tags, _)| tags.iter().any(|t| t.as_slice() == brand))
        .map(|(_, name)| *name)
}

/// Walk the top-level boxes and judge how much of the file agrees with the
/// format. A recognised brand in the file-type box settles it immediately;
/// otherwise confidence grows with the number of plausible boxes in a row, and
/// a run of them ending in something unknown keeps only what came before.
pub fn qt_probe(tf: &mut TypeFind) -> Option<(Prob, Option<&'static CStr>)> {
    let mut tip: Option<Prob> = None;
    let mut variant: Option<&'static CStr> = None;
    let mut row = 0usize;
    let mut have_moov = false;
    let mut have_mdat = false;
    let mut offset: u64 = 0;

    'walk: while let Some(head) = tf.peek::<12>(offset as i64) {
        // Held by value so that the later peeks for `largesize` and the whole
        // file-type box can borrow the stream again.
        let head = *head;

        if head[4..8] == *b"ftyp" {
            // A QuickTime movie is the format itself, so there is no dialect to
            // name.
            if head[8..12] == *b"qt  " {
                tip = Some(Prob::MAXIMUM);
                break;
            }
            if let Some(name) = dialect(&head[8..12], &MAJOR_DIALECTS) {
                tip = Some(Prob::MAXIMUM);
                variant = Some(name);
                break;
            }
        }

        if tag_at(&head, 4, COMMON_BOXES) {
            tip = Some(if tip.is_none() {
                Prob::LIKELY
            } else {
                Prob::NEARLY_CERTAIN
            });
            have_moov |= head[4..8] == *b"moov";
            have_mdat |= head[4..8] == *b"mdat";
            row += 1;
            // A presentation and its payload both present is the whole format.
            if (have_moov && have_mdat) || row >= 5 {
                tip = Some(Prob::MAXIMUM);
                break;
            }
        } else if tag_at(&head, 4, QT_ONLY_BOXES) {
            tip = Some(Prob::MAXIMUM);
            break;
        } else {
            // Unknown box. A short run proves nothing, so keep nothing.
            tip = (row >= 3).then_some(Prob::LIKELY);
            break;
        }

        let mut size = u32be(&head[..4]) as u64;
        if size
            .checked_add(offset)
            .is_none_or(|sum| sum >= i64::MAX as u64)
        {
            break;
        }

        // The major brand is only the first opinion a file gives. The brands
        // after it name the same dialects, and any one of them is enough. The
        // scan starts at 12, which is `minor_version`, so a field that happens
        // to spell a brand counts as one.
        if head[4..8] == *b"ftyp" && size >= 16 {
            // The declared size is bounded by what the stream actually holds; a
            // box that is not wholly present is not scanned, and the walk keeps
            // what it has found so far.
            let room = tf.length().map_or(FTYP_SCAN_MAX, |len| {
                len.get().saturating_sub(offset).min(FTYP_SCAN_MAX)
            });
            if size > room {
                break;
            }
            let Some(whole) = tf.peek_var(offset as i64, size as u32) else {
                break;
            };
            // A brand has to be whole, so the trailing partial one, if any, is
            // not read.
            for brand in whole[12..].as_chunks::<4>().0 {
                if let Some(name) = dialect(brand, &COMPAT_DIALECTS) {
                    tip = Some(Prob::MAXIMUM);
                    variant = Some(name);
                    break 'walk;
                }
            }
        }

        if size == 1 {
            let Some(ext) = tf.peek::<8>(offset as i64 + 8) else {
                break;
            };
            size = u64::from_be_bytes(*ext);
        } else if size < BOX_MIN {
            // Size 0 means the box runs to the end of the file, and 2 to 7 are
            // below the size of a box header. Neither can be stepped over.
            break;
        }

        let Some(next) = offset.checked_add(size) else {
            break;
        };
        if next <= offset || next.saturating_add(16) >= i64::MAX as u64 {
            break;
        }
        offset = next;
    }

    tip.map(|probability| (probability, variant))
}

// =============================================================================
// image/x-quicktime
// =============================================================================

/// A QuickTime image file carries its description and its pixels in two boxes
/// that nothing else uses. Both present settles it; one of them is a hint.
const IDSC: Tag = *b"idsc";
const IDAT: Tag = *b"idat";

/// How far to walk looking for the two boxes. A conforming file has them within
/// the first few, so this only bounds a search through junk.
const QTIF_MAX_ROUNDS: u32 = 25;

pub fn qtif_probe(tf: &mut TypeFind) -> Option<Prob> {
    let mut found_desc = false;
    let mut found_data = false;
    let mut offset: u64 = 0;

    for _ in 0..=QTIF_MAX_ROUNDS {
        let Some(head) = tf.peek::<8>(offset as i64) else {
            break;
        };
        let head = *head;
        let mut size = u32be(&head[..4]) as u64;
        if size == 1 {
            let Some(ext) = tf.peek::<8>(offset as i64 + 8) else {
                break;
            };
            size = u64::from_be_bytes(*ext);
        }
        if size < BOX_MIN {
            break;
        }

        found_desc |= head[4..8] == IDSC;
        found_data |= head[4..8] == IDAT;
        if found_desc && found_data {
            return Some(Prob::MAXIMUM);
        }

        // A size that carries the offset past the addressable range stops the
        // walk rather than wrapping back into the file.
        let Some(next) = offset.checked_add(size) else {
            break;
        };
        if next.saturating_add(8) >= i64::MAX as u64 {
            break;
        }
        offset = next;
    }

    (found_desc || found_data).then_some(Prob::LIKELY)
}

// =============================================================================
// application/x-3gp
// =============================================================================

/// The 3GPP file format brands, from TS 26.244. Only the first three octets are
/// compared, so every release in a family reports the same profile: `3gp4` and
/// `3gp6` both read as basic.
fn q3gp_profile(brand: &[u8]) -> Option<&'static CStr> {
    match &brand[..3] {
        b"3gg" => Some(c"general"),
        b"3gp" => Some(c"basic"),
        b"3gs" => Some(c"streaming-server"),
        b"3gr" => Some(c"progressive-download"),
        _ => None,
    }
}

/// The file-type box holds a major brand at offset 8, a minor version at 12,
/// and compatible brands from 16. Either brand may name a 3GPP profile.
pub fn q3gp_probe(tf: &mut TypeFind) -> Option<&'static CStr> {
    let head = tf.peek::<12>(0)?;
    if head[4..8] != *b"ftyp" {
        return None;
    }
    if let Some(profile) = q3gp_profile(&head[8..12]) {
        return Some(profile);
    }

    let size = u32be(&head[..4]);
    // A brand occupies 16 to 20, so below this the box holds no compatible
    // brand at all and there is nothing left to look at.
    if size < 20 {
        return None;
    }
    // Never ask the stream for more than it has, as in the QuickTime walk.
    let room = tf
        .length()
        .map_or(FTYP_SCAN_MAX, |len| len.get().min(FTYP_SCAN_MAX));
    if (size as u64) > room {
        return None;
    }
    let whole = tf.peek_var(0, size)?;

    let mut at = 16usize;
    while at + 4 <= whole.len() {
        if let Some(profile) = q3gp_profile(&whole[at..at + 4]) {
            return Some(profile);
        }
        at += 4;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn q3gp_families_match_their_named_brands() {
        // One brand per family from TS 26.244.
        assert_eq!(q3gp_profile(b"3gg6"), Some(c"general"));
        assert_eq!(q3gp_profile(b"3gp4"), Some(c"basic"));
        assert_eq!(q3gp_profile(b"3gs6"), Some(c"streaming-server"));
        assert_eq!(q3gp_profile(b"3gr6"), Some(c"progressive-download"));
    }

    #[test]
    fn every_release_in_a_family_reports_the_same_profile() {
        // Only three octets are compared, so the release digit is lost. TS
        // 26.244 lists 3gp4 to 3gp8 as distinct release brands, so this is
        // coarser than the specification by design.
        for release in b"45678" {
            let brand = [b'3', b'g', b'p', *release];
            assert_eq!(q3gp_profile(&brand), Some(c"basic"), "{:?}", brand);
        }
    }

    #[test]
    fn brands_from_other_dialects_are_not_3gp() {
        for other in [b"isom", b"qt  ", b"mp42", b"dash", b"ftyp"] {
            assert_eq!(q3gp_profile(other), None, "{:?}", other);
        }
    }

    #[test]
    fn a_major_brand_names_its_dialect() {
        assert_eq!(dialect(b"cmfc", &MAJOR_DIALECTS), Some(c"cmaf"));
        assert_eq!(dialect(b"isom", &MAJOR_DIALECTS), Some(c"iso"));
        assert_eq!(dialect(b"dash", &MAJOR_DIALECTS), Some(c"iso-fragmented"));
        assert_eq!(dialect(b"ccff", &MAJOR_DIALECTS), Some(c"ccff"));
        assert_eq!(dialect(b"mif1", &MAJOR_DIALECTS), Some(c"heif"));
        assert_eq!(dialect(b"junk", &MAJOR_DIALECTS), None);
    }

    #[test]
    fn ccff_names_no_dialect_as_a_compatible_brand() {
        assert_eq!(dialect(b"ccff", &COMPAT_DIALECTS), None);
        assert_eq!(dialect(b"cmfc", &COMPAT_DIALECTS), Some(c"cmaf"));
        assert_eq!(dialect(b"mif1", &COMPAT_DIALECTS), Some(c"heif"));
    }

    #[test]
    fn box_types_are_told_apart() {
        assert!(tag_at(b"\0\0\0\x10moov", 4, COMMON_BOXES));
        assert!(tag_at(b"\0\0\0\x10PICT", 4, QT_ONLY_BOXES));
        assert!(!tag_at(b"\0\0\0\x10moov", 4, QT_ONLY_BOXES));
        assert!(!tag_at(b"\0\0\0\x10PICT", 4, COMMON_BOXES));
    }
}
