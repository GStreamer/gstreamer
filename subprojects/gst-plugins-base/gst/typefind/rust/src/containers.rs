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
            // what it has found so far. The bound also caps the box at
            // FTYP_SCAN_MAX, far past any real ftyp: a declared box larger
            // than that is treated as absent, where C read the whole declared
            // span.
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
        // The stream holds no header at or past its end, and asking the
        // engine there reports a broken read rather than nothing. A box
        // declared as far as that ends the walk.
        if tf.length().is_some_and(|len| next >= len.get()) {
            break 'walk;
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
        // The stream holds no header at or past its end, and asking the
        // engine there reports a broken read rather than nothing. A box
        // declared as far as that ends the walk.
        if tf.length().is_some_and(|len| next >= len.get()) {
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

// =============================================================================
// video/x-matroska (EBML)
// =============================================================================

/// How far the top-level walk looks for the header material. A conforming
/// file keeps its EBML header and its `Tracks` well inside the first
/// megabytes; the bound is there to stop a walk through padding.
const EBML_WALK_MAX: u64 = 2 * 1024 * 1024;

/// An element header is an identifier of at most four octets and a size of
/// at most eight.
const EBML_HEAD: usize = 12;

/// The finder's opening window: the first sixty-four octets must be
/// present before the walk starts, so a header element opening there is
/// readable in full.
const EBML_MIN: usize = 64;

/// Recursion into nested elements stops here. A conforming file nests far
/// more shallowly than this; the bound stops a walk through data that
/// recycles as headers.
const EBML_DEPTH_MAX: u8 = 8;

// Ids are the values the identifier octets spell as one big-endian integer,
// as the Matroska element specification lists them.
const EBML_HEADER: u64 = 0x1A45_DFA3;
/// The EBML header identifier as its four octets, for the opening test.
const EBML_HEADER_ID: [u8; 4] = [0x1A, 0x45, 0xDF, 0xA3];
const DOCTYPE: u64 = 0x4282;
const SEGMENT: u64 = 0x1853_8067;
const TRACKS: u64 = 0x1654_AE6B;
const TRACK_ENTRY: u64 = 0xAE;
const TRACK_TYPE: u64 = 0x83;
const STEREO_MODE: u64 = 0x53B8;
const CLUSTER: u64 = 0x1F43_B675;

/// What the document type declared, and the `Tracks` counted so far.
#[derive(Default)]
struct Ebml {
    doctype: Option<DocType>,
    video: u32,
    audio: u32,
    other: u32,
    stereo: u32,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum DocType {
    Matroska,
    Webm,
}

/// What one element header claimed. An `Unknown` size is legal only for
/// `Segment`, whose interior is then read until its last child ends.
struct Element {
    id: u64,
    size: Option<u64>,
    head: usize,
}

/// One EBML element header at the head of `d`, the identifier as a
/// big-endian integer and the size as octets to skip. The identifier's
/// length class is the run of zero bits opening its first octet; the size
/// VINT keeps its own class bit and loses no data bits, and an all-ones
/// `VINT_DATA` is the reserved unknown size.
fn ebml_header(d: &[u8]) -> Option<Element> {
    let lead = d.first()?;
    if *lead == 0 {
        return None;
    }
    let id_len = 1 + lead.leading_zeros() as usize;
    if id_len > 4 || d.len() < id_len + 1 {
        return None;
    }
    let id = d[..id_len]
        .iter()
        .fold(0u64, |v, b| (v << 8) | u64::from(*b));

    let size_lead = d[id_len];
    if size_lead == 0 {
        return None;
    }
    let size_len = 1 + size_lead.leading_zeros() as usize;
    if size_len > 8 || d.len() < id_len + size_len {
        return None;
    }
    let data = &d[id_len..id_len + size_len];
    // The marker bit and the data share the lead octet, so the data mask
    // is one shift wider than the class. The eight-octet class has no
    // marker room left in the lead octet and masks it away entirely.
    let mask = 0xFFu8.checked_shr(size_len as u32).unwrap_or(0);
    if data[0] & mask == mask && data[1..].iter().all(|b| *b == 0xFF) {
        return Some(Element {
            id,
            size: None,
            head: id_len + size_len,
        });
    }
    let size = data
        .iter()
        .skip(1)
        .fold(u64::from(data[0] & mask), |v, b| (v << 8) | u64::from(*b));
    Some(Element {
        id,
        size: Some(size),
        head: id_len + size_len,
    })
}

impl Ebml {
    /// Walk the elements in `range`, the interior of an element at `depth`
    /// (the document walk is depth 0). A child that names nothing is
    /// stepped over, which is what lets padding pass; a header that is no
    /// header at all ends the walk, because nothing behind it can be
    /// located again.
    fn parse(&mut self, tf: &mut TypeFind, range: (u64, u64), depth: u8) -> bool {
        let (start, end) = range;
        let mut at = start;

        while at < end {
            let Some(head) = tf.peek::<EBML_HEAD>(at as i64) else {
                return true;
            };
            let Some(el) = ebml_header(head) else {
                return false;
            };

            let interior = match el.size {
                Some(size) => {
                    let Some(body) = at.checked_add(el.head as u64) else {
                        return true;
                    };
                    let Some(child_end) = body.checked_add(size) else {
                        return false;
                    };
                    (body, end.min(child_end))
                }
                None => {
                    // Unknown size: only a Segment may leave its end open.
                    if el.id != SEGMENT {
                        return false;
                    }
                    (at + el.head as u64, end)
                }
            };

            match el.id {
                DOCTYPE => {
                    let [lo, hi] = [interior.0, interior.1.min(interior.0 + 32)];
                    if hi > lo
                        && let Some(value) = tf.peek_var(lo as i64, (hi - lo) as u32)
                    {
                        if value.starts_with(b"matroska") {
                            self.doctype = Some(DocType::Matroska);
                        } else if value.starts_with(b"webm") {
                            self.doctype = Some(DocType::Webm);
                        }
                    }
                    at = interior.1;
                }
                SEGMENT if depth == 0 => {
                    if !self.parse(tf, interior, depth + 1) {
                        return false;
                    }
                    break; // The walk below a Segment says all the top level has to say.
                }
                EBML_HEADER if depth == 0 => {
                    // The document type is an element of the header. A
                    // header that stops parsing is still an EBML header;
                    // the walk keeps what it found.
                    let _ = self.parse(tf, interior, depth + 1);
                    at = interior.1;
                }
                TRACKS => {
                    if depth < EBML_DEPTH_MAX {
                        let _ = self.parse(tf, interior, depth + 1);
                    }
                    break;
                }
                TRACK_ENTRY if depth < EBML_DEPTH_MAX => {
                    if !self.parse(tf, interior, depth + 1) {
                        return false;
                    }
                    at = interior.1;
                }
                TRACK_TYPE => {
                    let [lo, hi] = [interior.0, interior.1.min(interior.0 + 8)];
                    if hi > lo
                        && let Some(value) = tf.peek_var(lo as i64, (hi - lo) as u32)
                    {
                        // The kind as one big-endian integer. The video
                        // test reads that integer whole; the audio test
                        // reads only the first octet, so a multi-octet
                        // value opening with 2 counts as audio. The
                        // finder's own asymmetry, kept as is.
                        let kind = value.iter().fold(0u64, |v, b| (v << 8) | u64::from(*b));
                        if kind == 1 {
                            self.video += 1;
                        } else if value[0] == 2 {
                            self.audio += 1;
                        } else {
                            self.other += 1;
                        }
                    }
                    at = interior.1;
                }
                STEREO_MODE => {
                    self.stereo += 1;
                    at = interior.1;
                }
                CLUSTER => return true,
                _ => at = interior.1,
            }
        }
        true
    }
}

/// A Matroska or WebM stream: an EBML header, then a Segment or the
/// `Tracks` of one, seen from the first two megabytes. The document type
/// settles the confidence; the track kinds settle the media type, with
/// stereo video as its own.
pub fn matroska_probe(tf: &mut TypeFind) -> Option<(&'static CStr, Prob)> {
    // Requires the opening window and the EBML header identifier at its
    // head.
    let head = tf.peek::<EBML_MIN>(0)?;
    if head[..4] != EBML_HEADER_ID {
        return None;
    }
    let mut ebml = Ebml::default();
    if !ebml.parse(tf, (0, EBML_WALK_MAX), 0) {
        return None;
    }
    let is_webm = ebml.doctype == Some(DocType::Webm);
    let is_audio = ebml.audio > 0 && ebml.video == 0 && ebml.other == 0;
    let caps = match (is_webm, is_audio, ebml.video > 0 && ebml.stereo > 0) {
        (true, true, _) => c"audio/webm",
        (true, false, _) => c"video/webm",
        (false, _, true) => c"video/x-matroska-3d",
        (false, true, false) => c"audio/x-matroska",
        (false, false, false) => c"video/x-matroska",
    };
    // A recognised document type says what the elements mean; an unknown
    // one is still an EBML stream with tracks, so the finder stays a step
    // back.
    let prob = if ebml.doctype.is_some() {
        Prob::MAXIMUM
    } else {
        Prob::LIKELY
    };
    Some((caps, prob))
}

// =============================================================================
// application/mxf
// =============================================================================

/// The key of an MXF header partition pack. A header metadata pack follows
/// at a distance, so a file can begin anywhere; the finder searches the
/// window for the pack rather than expecting it at the head.
pub(crate) const MXF_HEADER_PACK: [u8; 13] = [
    0x06, 0x0E, 0x2B, 0x34, 0x02, 0x05, 0x01, 0x01, 0x0D, 0x01, 0x02, 0x01, 0x01,
];

/// How far the key is chased. A conforming file leads with the header
/// partition pack; the window only bounds a hunt through leading junk.
pub(crate) const MXF_SCAN_MAX: usize = 64 * 1024;

/// A pack key opens with the sixteen-octet run the key shares, a `02` pack
/// type, a partition status below five and a reserved octet.
pub(crate) fn mxf_partition_pack(data: &[u8]) -> bool {
    data.windows(16)
        .any(|w| w[..13] == MXF_HEADER_PACK && w[13] == 0x02 && w[14] < 5 && w[15] == 0)
}

// =============================================================================
// application/x-pxstr
// =============================================================================

// A raw CD stream, read as sectors of one frame each. The spec's Notes
// record the source loop as inverted, making the finder never suggest;
// this is the documented intent, per the house rule that ports follow
// the documented behaviour of a recorded defect (07-notes, bugs table).

const PXSTR_SECTOR: usize = 2352;
const PXSTR_SECTORS_MAX: usize = 5;
const PXSTR_SYNC: [u8; 12] = [
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
];

pub(crate) fn pxstr_probe(tf: &mut TypeFind) -> Option<Prob> {
    let data = tf.peek_at_most(PXSTR_SECTORS_MAX * PXSTR_SECTOR)?;
    if data.len() < PXSTR_SECTOR {
        return None;
    }
    // A CDXA wrapper puts a chunk header ahead of the sectors. The
    // window is taken before the header is dropped, so a wrapped file
    // reaches four whole sectors and the head of a fifth at best.
    let body = if data.starts_with(b"RIFF") && data[8..12] == *b"CDXA" {
        &data[0x2C..]
    } else {
        data
    };
    let mut counted = 0u32;
    let mut at = 0usize;
    while at + PXSTR_SECTOR <= body.len() {
        let sector = &body[at..at + PXSTR_SECTOR];
        let mode = sector[0x12] & 0x0E;
        if !pxstr_sector(sector, mode) {
            break;
        }
        counted += u32::from(mode != 0);
        at += PXSTR_SECTOR;
    }
    if counted > 3 {
        Some(Prob::POSSIBLE)
    } else if counted > 0 {
        Some(Prob::MINIMUM)
    } else {
        None
    }
}

/// One sector's claim: the frame sync and a video mode whose geometry
/// adds up, or an audio mode with its reserved bits clear. A sector
/// named neither is stepped over; anything named and wrong ends the
/// walk.
fn pxstr_sector(sector: &[u8], mode: u8) -> bool {
    sector[..12] == PXSTR_SYNC
        && sector[0x11] < 32
        && match mode {
            0x08 | 0x02 => {
                let current = i16::from_le_bytes([sector[0x1C], sector[0x1D]]) as i32;
                let count = i16::from_le_bytes([sector[0x1E], sector[0x1F]]) as i32;
                let frame = i16::from_le_bytes([sector[0x24], sector[0x25]]) as i32;
                frame > 0 && current < count && count * 0x780 >= frame
            }
            0x04 => sector[0x13] & 0x2A == 0,
            0 => true,
            _ => false,
        }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::helpers::probe_engine::probe;

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

    /// The identifier octets and size octets of a Matroska element.
    fn eid(id: u64) -> Vec<u8> {
        id.to_be_bytes()
            .into_iter()
            .skip_while(|b| *b == 0)
            .collect()
    }

    /// A short element: id, one-octet size VINT, then `value`.
    fn el(id: u64, value: &[u8]) -> Vec<u8> {
        assert!(value.len() < 128, "one-octet size only");
        let mut v = eid(id);
        v.push(0x80 | value.len() as u8);
        v.extend_from_slice(value);
        v
    }

    /// A master element holding `body` whole.
    fn master(id: u64, body: &[u8]) -> Vec<u8> {
        el(id, body)
    }

    /// A track of the given kind: video tracks carry no stereo
    /// declaration, so only the stereo case adds one.
    fn track(kind: u8) -> Vec<u8> {
        master(TRACK_ENTRY, &el(TRACK_TYPE, &[kind]))
    }

    fn probe_matroska(data: &[u8]) -> Option<(&'static CStr, Prob)> {
        probe(data, matroska_probe)
    }

    /// A log whose first octets read `0:00` spells a box header of
    /// 772 MiB in a stream that is megabytes shorter. The walk reads
    /// the stream length to stop at its end; asking the engine at or
    /// past that end reports a broken read, which the element turns
    /// into an error and discards every finder's answer.
    #[test]
    fn a_box_claiming_past_the_end_ends_the_walks() {
        // The size octets spell 772 MiB; the box type is a real one, so
        // the walk has a reason to keep going past this box.
        let mut data = b"0:00moov".to_vec();
        data.resize(8192, b'a');
        assert!(probe(&data, qtif_probe).is_none());
        // The presentation box was seen before the claim, and a walk kept
        // to the stream's end keeps what it has found.
        assert_eq!(probe(&data, qt_probe), Some((Prob::LIKELY, None)));
    }

    /// The bound reads the stream length rather than trusting a claimed
    /// box size: a `largesize` header claiming four GiB ends the walk
    /// the same way a short one does, without a read the stream cannot
    /// answer.
    #[test]
    fn a_largesize_past_the_end_ends_the_walk_too() {
        let mut data = (8u32).to_be_bytes().to_vec(); // one header-only box
        data.extend(*b"junk");
        data.extend(1u32.to_be_bytes()); // largesize follows
        data.extend(*b"junk");
        data.extend((1u64 << 32).to_be_bytes());
        data.resize(8192, 0x00);
        assert!(probe(&data, qtif_probe).is_none());
        assert!(probe(&data, qt_probe).is_none());
    }

    /// Append Void elements until the stream reaches at least `min_len`:
    /// a finder requires the opening sixty-four octets, and every element
    /// header is read at a twelve-octet window that a short tail would
    /// cut off. Void names nothing, so the walk steps over it to the end.
    fn pad(mut data: Vec<u8>, min_len: usize) -> Vec<u8> {
        while data.len() < min_len {
            let mut void_ = vec![0xEC, 0x80 | 14];
            void_.resize(void_.len() + 14, 0x00);
            data.extend(void_);
        }
        data
    }

    /// A conforming stream opens with the EBML header carrying the
    /// document type, then the segment, then the tracks.
    fn mkv(kinds: &[u8]) -> Vec<u8> {
        let tracks = master(
            TRACKS,
            &kinds.iter().flat_map(|k| track(*k)).collect::<Vec<u8>>(),
        );
        let ebml = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        let mut data = ebml;
        data.extend(master(SEGMENT, &tracks));
        data.resize(64, 0xFF);
        data
    }

    /// The VINT: identifier and size both derive their length from the
    /// leading zeros, the size keeps its marker bit, and all-ones data is
    /// the reserved unknown size.
    #[test]
    fn ebml_headers_read_as_the_vint_they_are() {
        let e = ebml_header(b"\x1a\x45\xdf\xa3\x84\x1a\x45\xdf\xa3").unwrap();
        assert_eq!(e.id, EBML_HEADER);
        assert_eq!(e.size, Some(4));
        assert_eq!(e.head, 5);
        let e = ebml_header(b"\x42\x82\x83abc").unwrap();
        assert_eq!(e.id, DOCTYPE);
        assert_eq!(e.size, Some(3));
        let e = ebml_header(b"\x10\x00\x00\x00\x02\xff\xff\xff\xff\xff\xff\xff").unwrap();
        assert_eq!(e.id, 0x1000_0000);
        assert_eq!(e.size, Some(0xFFFF_FFFF_FFFF));
        // All 63 data bits set is the reserved unknown size.
        let e = ebml_header(b"\x18\x53\x80\x67\x01\xff\xff\xff\xff\xff\xff\xff").unwrap();
        assert_eq!(e.id, SEGMENT);
        assert_eq!(e.size, None);
        // The six-octet class likewise: all ones reserved, the same
        // octets with the lead's data bit clear a real, merely huge, size.
        let e = ebml_header(b"\x18\x53\x80\x67\x03\xff\xff\xff\xff\xff\xff").unwrap();
        assert_eq!(e.size, None);
        let e = ebml_header(b"\x18\x53\x80\x67\x02\xff\xff\xff\xff\xff\xff").unwrap();
        assert_eq!(e.size, Some(0xFFFF_FFFF_FFFF));
    }

    /// A header the format cannot produce: a zero lead octet anywhere, a
    /// size class that overruns the buffer, or an identifier past its
    /// longest class.
    #[test]
    fn ebml_headers_the_format_cannot_produce() {
        assert!(ebml_header(b"\x00\x80\x80\x80\x01").is_none());
        assert!(ebml_header(b"\x1a\x45\xdf\xa3\x00").is_none());
        assert!(ebml_header(b"\x08\x00\x00\x00\x00\x00\x81\x01\xFF").is_none());
        // Two-octet id whose size lead is missing, and one whose size
        // bytes run past the end.
        assert!(ebml_header(b"\x42").is_none());
        assert!(ebml_header(b"\x42\x82\x40").is_none());
    }

    /// The document type decides the family and the confidence; the track
    /// kinds decide the media type.
    #[test]
    fn doctype_and_tracks_decide_the_answer() {
        assert_eq!(
            probe_matroska(&mkv(&[1])),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
        assert_eq!(
            probe_matroska(&mkv(&[2])),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
        // A complex (3) or subtitle (17) track keeps the stream from being
        // audio-only.
        assert_eq!(
            probe_matroska(&mkv(&[2, 3])),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );

        // Unknown document type: the elements still read, one step back.
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"not-a-doctype"));
        data.extend(master(SEGMENT, &master(TRACKS, &track(1))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska", Prob::LIKELY))
        );
        // No document type at all: same standing.
        let mut data = master(EBML_HEADER, b"");
        data.extend(master(SEGMENT, &master(TRACKS, &track(1))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska", Prob::LIKELY))
        );
    }

    /// Stereo video is its own answer: a track that counts as video and
    /// declares a stereo mode.
    #[test]
    fn stereo_video_is_its_own_answer() {
        let entry = master(
            TRACK_ENTRY,
            &[el(STEREO_MODE, &[0]), el(TRACK_TYPE, &[1])].concat(),
        );
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(master(SEGMENT, &master(TRACKS, &entry)));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska-3d", Prob::MAXIMUM))
        );
    }

    /// WebM answers as WebM whatever it holds; the document type is the
    /// difference between the two families.
    #[test]
    fn webm_uses_its_own_media_types() {
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"webm"));
        data.extend(master(SEGMENT, &master(TRACKS, &track(1))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/webm", Prob::MAXIMUM))
        );
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"webm"));
        data.extend(master(SEGMENT, &master(TRACKS, &track(2))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"audio/webm", Prob::MAXIMUM))
        );
    }

    /// The unknown segment size is the one place a master may leave its
    /// end open; the walk runs to the end of the probe window.
    #[test]
    fn an_unknown_size_segment_is_read_to_the_end() {
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(eid(SEGMENT));
        data.extend([0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]); // unknown size
        data.extend(master(TRACKS, &track(2)));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
        // Unknown size on any other element fails the parse. Inside the
        // segment, ahead of the tracks, the failure is the answer.
        let mut seg = eid(CLUSTER);
        seg.extend([0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]);
        seg.extend(master(TRACKS, &track(2)));
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(master(SEGMENT, &seg));
        assert_eq!(probe_matroska(&pad(data, 64)), None);
    }

    /// A cluster stops the walk where it stands, so the audio track that
    /// follows it inside the same segment is never read and the stream
    /// answers as video.
    #[test]
    fn a_cluster_ends_the_walk() {
        let mut seg = master(CLUSTER, &[0x00, 0x00]);
        seg.extend(master(TRACKS, &track(2)));
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(master(SEGMENT, &seg));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
    }

    /// A stream shorter than the opening window, or one whose opening is
    /// not the EBML header, answers nothing.
    #[test]
    fn short_streams_and_foreign_leads() {
        // Under the opening window, even a correct EBML header is not read.
        assert_eq!(
            probe_matroska(&master(EBML_HEADER, &el(DOCTYPE, b"matroska"))),
            None
        );
        // The header alone, given its window, answers at full confidence.
        assert_eq!(
            probe_matroska(&pad(master(EBML_HEADER, &el(DOCTYPE, b"matroska")), 64)),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
        // A stream that runs off into bytes that are no element: the
        // parse fails and nothing is settled.
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.resize(64, 0xFF);
        assert_eq!(probe_matroska(&data), None);
        // Not even the EBML header.
        assert_eq!(probe_matroska(&mkv(&[1])[1..]), None);
    }

    /// The document type is read from the beginning of the value, up to
    /// what a plausible DocType holds.
    #[test]
    fn doctype_is_matched_at_the_head_of_its_value() {
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska-extension"));
        data.extend(master(SEGMENT, &master(TRACKS, &track(1))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
        // The family word must lead: a value that merely contains it is
        // not the family.
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"xmatroska"));
        data.extend(master(SEGMENT, &master(TRACKS, &track(1))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"video/x-matroska", Prob::LIKELY))
        );
    }

    // ---------------------------------------------------------------------
    // matroska: walk limits and value readings
    // ---------------------------------------------------------------------

    /// Only the top of the document may hold the header and the segment:
    /// one nested in another is stepped over, contents unread. A header
    /// whose interior stops parsing is still a header: the walk carries
    /// what the interior settled.
    #[test]
    fn master_elements_below_the_top_are_stepped_over() {
        // The nested segment's video track stays unseen; the outer tracks
        // answer alone.
        let inner = master(SEGMENT, &track(1));
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(master(
            SEGMENT,
            &[inner, master(TRACKS, &track(2))].concat(),
        ));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
        // Junk behind the document type inside the header kills the
        // header's own parse, not the answer the type already earned.
        let mut interior = el(DOCTYPE, b"matroska");
        interior.push(0x00); // no element opens with this
        let mut data = master(EBML_HEADER, &interior);
        data.extend(master(SEGMENT, &master(TRACKS, &track(2))));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
    }

    /// A track entry buried deeper than the walk descends is padding: the
    /// shallow audio track answers alone.
    #[test]
    fn tracks_below_the_depth_cap_are_not_counted() {
        let mut deep = track(1);
        for _ in 0..7 {
            deep = master(TRACK_ENTRY, &deep);
        }
        let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
        data.extend(master(SEGMENT, &master(TRACKS, &[track(2), deep].concat())));
        assert_eq!(
            probe_matroska(&pad(data, 64)),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
    }

    /// A Segment that claims gigabytes is ordinary for a rip: the walk
    /// bounds the interior by what the stream holds, so a large claim is
    /// stepped into, not rejected. A class-five VINT carries 35 data
    /// bits: six GiB lights the bit beside its lead marker, four GiB is
    /// the next class boundary down.
    #[test]
    fn a_huge_segment_still_walks() {
        let claiming = |size: [u8; 5]| {
            let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
            data.extend(eid(SEGMENT));
            data.extend(size);
            data.extend(master(TRACKS, &track(1)));
            probe_matroska(&pad(data, EBML_MIN))
        };
        assert_eq!(
            claiming([0x09, 0x80, 0x00, 0x00, 0x00]), // six GiB
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
        assert_eq!(
            claiming([0x09, 0x00, 0x00, 0x00, 0x00]), // four GiB
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
    }

    /// A run that panics propagates: the guard's boundary is the whole
    /// run, so a half-run neither returns a value nor keeps suggestions —
    /// what an aborted run holds is not evidence of anything.
    #[test]
    #[should_panic(expected = "half a search")]
    fn a_panicking_run_propagates() {
        crate::helpers::probe_engine::probe_suggestions(
            &master(EBML_HEADER, &el(DOCTYPE, b"matroska")),
            |_tf| panic!("half a search"),
        );
    }

    /// The kind test reads the value as one big-endian integer for video
    /// and only its first octet for audio: a multi-octet value opening
    /// with 2 is audio, and one spelling one across octets is video.
    /// (The reading caps at eight octets; see the deviation note on the
    /// Matroska entry of the spec.)
    #[test]
    fn track_kinds_follow_the_finders_own_reading() {
        let entry_with = |value: &[u8]| master(TRACK_ENTRY, &el(TRACK_TYPE, value));
        let tracks_of = |entries: &[Vec<u8>]| -> Option<(&'static CStr, Prob)> {
            let mut data = master(EBML_HEADER, &el(DOCTYPE, b"matroska"));
            data.extend(master(SEGMENT, &master(TRACKS, &entries.concat())));
            probe_matroska(&pad(data, 64))
        };
        // The eight-octet spelling of one reads whole: video, and with a
        // stereo mode in the entry the answer is the three-dimensional.
        let one8 = {
            let mut e = entry_with(&[0, 0, 0, 0, 0, 0, 0, 1]);
            e.extend(master(STEREO_MODE, &[0]));
            e
        };
        assert_eq!(
            tracks_of(&[one8]),
            Some((c"video/x-matroska-3d", Prob::MAXIMUM))
        );
        // Opening with 2 across two octets still counts as audio.
        assert_eq!(
            tracks_of(&[entry_with(&[0x02, 0x00])]),
            Some((c"audio/x-matroska", Prob::MAXIMUM))
        );
        // One in trailing position is neither kind: not audio-only.
        assert_eq!(
            tracks_of(&[entry_with(&[0x02, 0x00]), entry_with(&[0x00, 0x01])]),
            Some((c"video/x-matroska", Prob::MAXIMUM))
        );
    }

    // ---------------------------------------------------------------------
    // mxf
    // ---------------------------------------------------------------------

    /// A pack key sits at the head of a file or anywhere inside the first
    /// window; the pack type and partition status behind it close the
    /// claim.
    #[test]
    fn mxf_pack_key_is_read_wherever_it_sits() {
        let mut pack = MXF_HEADER_PACK.to_vec();
        pack.extend([0x02, 0x02, 0x00]);
        assert!(mxf_partition_pack(&pack));

        let mut junk = vec![0u8; 511];
        junk.extend_from_slice(&pack);
        assert!(mxf_partition_pack(&junk));

        // Any of the three bytes behind the key can say otherwise.
        for tail in [[0x01u8, 0x02, 0x00], [0x02, 0x05, 0x00], [0x02, 0x02, 0x01]] {
            let mut near = MXF_HEADER_PACK.to_vec();
            near.extend(tail);
            assert!(!mxf_partition_pack(&near), "{tail:?}");
        }

        // The key's last octet distinguishes it from the other pack keys
        // of the same family.
        let mut near = MXF_HEADER_PACK.to_vec();
        near[12] ^= 0x01;
        near.extend([0x02, 0x02, 0x00]);
        assert!(!mxf_partition_pack(&near));

        // A key truncated at the window end is not read.
        let mut truncated = vec![0u8; 64 * 1024 - 8];
        truncated.extend_from_slice(&MXF_HEADER_PACK[..10]);
        assert!(!mxf_partition_pack(&truncated));
    }

    // ---------------------------------------------------------------------
    // pxstr
    // ---------------------------------------------------------------------

    fn pxstr_sector_bytes(mode: u8, current: i16, count: i16, frame: i16) -> Vec<u8> {
        let mut s = vec![0u8; PXSTR_SECTOR];
        s[..12].copy_from_slice(&PXSTR_SYNC);
        s[0x12] = mode;
        s[0x1C..0x1E].copy_from_slice(&current.to_le_bytes());
        s[0x1E..0x20].copy_from_slice(&count.to_le_bytes());
        s[0x24..0x26].copy_from_slice(&frame.to_le_bytes());
        s
    }

    fn video() -> Vec<u8> {
        pxstr_sector_bytes(0x08, 0, 4, 0x780)
    }

    fn audio() -> Vec<u8> {
        pxstr_sector_bytes(0x04, 0, 0, 0)
    }

    fn pxstr_walk(sectors: &[&[u8]]) -> Option<Prob> {
        let data: Vec<u8> = sectors.iter().flat_map(|s| s.iter().copied()).collect();
        probe(&data, pxstr_probe)
    }

    /// Four whole sectors of a named mode answer at the possible tier;
    /// fewer are a whisper, none is silence. Both video arms count, the
    /// mode byte is masked, and equality of a frame with the room that
    /// holds it still counts. The sectors may sit bare or behind a CDXA
    /// chunk header.
    #[test]
    fn pxstr_counts_named_sectors() {
        assert_eq!(
            pxstr_walk(&[&video(), &video(), &video(), &video()]),
            Some(Prob::POSSIBLE)
        );
        // The second video arm, 0x02, is the first one's equal.
        let mut mode2 = video();
        mode2[0x12] = 0x02;
        assert_eq!(
            pxstr_walk(&[&mode2, &mode2, &mode2, &mode2]),
            Some(Prob::POSSIBLE)
        );
        assert_eq!(
            pxstr_walk(&[&audio(), &audio(), &video(), &video()]),
            Some(Prob::POSSIBLE)
        );
        assert_eq!(
            pxstr_walk(&[&video(), &video(), &video()]),
            Some(Prob::MINIMUM)
        );
        assert_eq!(pxstr_walk(&[&audio()]), Some(Prob::MINIMUM));
        // The mode byte is read through its mask: 0x09 is video.
        let mut masked = video();
        masked[0x12] = 0x09;
        assert_eq!(probe(&masked, pxstr_probe), Some(Prob::MINIMUM));
        // A frame filling the room that holds it to the last byte is
        // still a frame that fits.
        assert_eq!(
            probe(&pxstr_sector_bytes(0x08, 0, 1, 0x780), pxstr_probe),
            Some(Prob::MINIMUM)
        );
        // The audio reserved bits are read whole: any of 0x2A set is
        // silence, anything else passes.
        for (reserved, expected) in [
            (0x00u8, Some(Prob::MINIMUM)),
            (0x05, Some(Prob::MINIMUM)),
            (0x02, None),
            (0x08, None),
            (0x20, None),
        ] {
            let mut s = audio();
            s[0x13] = reserved;
            assert_eq!(probe(&s, pxstr_probe), expected, "reserved {reserved:#04x}");
        }
        // Nothing named, nothing said: a mode-zero sector is stepped
        // over, not an end — four named across five sectors answer.
        let mode_zero = pxstr_sector_bytes(0x00, 0, 0, 0);
        assert_eq!(pxstr_walk(&[&mode_zero]), None);
        assert_eq!(
            pxstr_walk(&[&video(), &mode_zero, &video(), &video(), &video()]),
            Some(Prob::POSSIBLE)
        );
        // The CDXA wrapper precedes the sectors; what follows reads the same.
        let mut wrapped = b"RIFF".to_vec();
        wrapped.extend(0u32.to_le_bytes());
        wrapped.extend_from_slice(b"CDXA");
        wrapped.resize(0x2C, 0x00);
        let bare: Vec<u8> = [video(), video(), video(), video()].concat();
        let with_header = [wrapped, bare.clone()].concat();
        assert_eq!(probe(&with_header, pxstr_probe), Some(Prob::POSSIBLE));
        assert_eq!(probe(&bare, pxstr_probe), Some(Prob::POSSIBLE));
    }

    /// A sector that fails the sync, the channel byte, the video
    /// geometry, the audio reserved bits or the mode table ends the
    /// walk; what was counted before it stands and nothing behind it
    /// counts. Three named sectors ahead of the bad one put the answer
    /// one step below the tier a fourth would reach: were the bad
    /// sector stepped over instead, the fifth would lift it.
    #[test]
    fn pxstr_first_bad_sector_ends_the_walk() {
        let good = video();
        let mut no_sync = good.clone();
        no_sync[5] = 0x00;
        let mut channel_32 = good.clone();
        channel_32[0x11] = 32;
        let mut frame_zero = good.clone();
        frame_zero[0x24..0x26].copy_from_slice(&0i16.to_le_bytes());
        let mut current_eq_count = good.clone();
        current_eq_count[0x1C..0x20].copy_from_slice(&[2, 0, 2, 0]);
        // One sector holds 0x780 bytes of frame; a frame needing more
        // cannot fit the count that holds it.
        let frame_too_big = pxstr_sector_bytes(0x08, 0, 1, 0x781);
        let mut count_negative = good.clone();
        count_negative[0x1E..0x20].copy_from_slice(&(-1i16).to_le_bytes());
        let mut audio_reserved = audio();
        audio_reserved[0x13] = 0x08;
        let mut mode_masks_to_reserved = good.clone();
        mode_masks_to_reserved[0x12] = 0x0D; // & 0x0E = 0x0C
        let mut mode_unnamed = good.clone();
        mode_unnamed[0x12] = 0x06;

        for bad in [
            no_sync,
            channel_32,
            frame_zero,
            current_eq_count,
            frame_too_big,
            count_negative,
            audio_reserved,
            mode_unnamed,
            mode_masks_to_reserved,
        ] {
            // Three named sectors before it are kept; the bad one and
            // all behind it are not.
            assert_eq!(
                pxstr_walk(&[&good, &good, &good, &bad, &good]),
                Some(Prob::MINIMUM),
                "bad sector {:?}",
                &bad[..0x28]
            );
        }
        // The channel byte is read up to its bound: 31 passes where 32
        // ends the walk.
        let mut channel_31 = good.clone();
        channel_31[0x11] = 31;
        assert_eq!(
            pxstr_walk(&[&channel_31, &good, &good, &good]),
            Some(Prob::POSSIBLE)
        );
    }

    /// One whole sector is the smallest thing the finder will read,
    /// and five sectors is as far as it reaches: named sectors behind
    /// the fifth are not read.
    #[test]
    fn pxstr_wants_a_whole_sector_and_reads_five() {
        let one = video();
        assert_eq!(probe(&one[..PXSTR_SECTOR - 1], pxstr_probe), None);
        assert_eq!(probe(&one, pxstr_probe), Some(Prob::MINIMUM));
        // Five mode-zero sectors fill the window; the four video
        // sectors behind it are never reached.
        let mode_zero = pxstr_sector_bytes(0x00, 0, 0, 0);
        assert_eq!(
            pxstr_walk(&[
                &mode_zero,
                &mode_zero,
                &mode_zero,
                &mode_zero,
                &mode_zero,
                &video(),
                &video(),
                &video(),
                &video()
            ]),
            None
        );
    }
}
