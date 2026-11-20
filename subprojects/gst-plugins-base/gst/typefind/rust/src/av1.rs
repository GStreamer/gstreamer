// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Probes for the AV1 bitstream, which reaches a file in two framings. The
//! plain one is a bare run of Open Bitstream Units; the other groups them
//! into temporal units and prefixes each unit, frame and OBU with a LEB128
//! length, which is what GStreamer names the `annexb` stream format.
//!
//! Both framings open with the same OBU header byte, and the length-prefixed
//! one with three of those lengths before its first OBU, so the probe reads
//! no more than the header arithmetic: twenty-five octets at offset 0.

use crate::helpers::Prob;
use crate::scanner::TypeFind;
use std::ffi::CStr;

/// The largest prefix a LEB128 can occupy, in octets.
const LEB128_MAX: usize = 8;

/// The framings, as the `stream-format` field names them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Av1Format {
    /// A bare run of Open Bitstream Units.
    ObuStream,
    /// Temporal units, each prefixed with a LEB128 length.
    Annexb,
}

impl Av1Format {
    pub fn as_str(self) -> &'static CStr {
        match self {
            Av1Format::ObuStream => c"obu-stream",
            Av1Format::Annexb => c"annexb",
        }
    }
}

/// An OBU header octet without its flags. The AV1 specification, 6.2.2,
/// assigns the types that carry data; a header with the forbidden bit or the
/// reserved bit set, or with a type that names nothing, cannot open a
/// stream. The flags come back with the type: the extension flag says an
/// extension octet follows, the size flag a LEB128 size.
fn obu_header(b: u8) -> Option<(bool, bool)> {
    if b & 0x81 != 0 {
        return None;
    }
    match (b >> 3) & 0x0F {
        1..=8 | 15 => Some((b & 0x04 != 0, b & 0x02 != 0)),
        _ => None,
    }
}

/// The value of the LEB128 at the head of `d` and its length in octets, as
/// the specification's parsing process reads it: seven payload bits per
/// octet, the high bit a continuation, no more than eight octets, and no
/// value beyond four GiB minus one.
fn leb128(d: &[u8]) -> Option<(u64, usize)> {
    let mut value: u64 = 0;
    for (i, b) in d.iter().enumerate().take(LEB128_MAX) {
        value |= u64::from(b & 0x7F) << (i * 7);
        if value > u64::from(u32::MAX) {
            return None;
        }
        if b & 0x80 == 0 {
            return Some((value, i + 1));
        }
    }
    None
}

/// How the twenty-five leading octets of a stream are framed, if they are
/// AV1 at all: a bare OBU parses wholly, and failing that a temporal unit
/// carries the two length prefixes that put a temporal delimiter (type 2)
/// first.
///
/// A temporal delimiter is empty, so in the length-prefixed framing it is
/// one octet and carries no size field: its header octet is the last byte
/// the unit's own lengths can reach. A unit whose arithmetic cannot place
/// that octet inside itself is a coincidence, not a delimiter.
pub fn av1_probe(tf: &mut TypeFind) -> Option<(&'static CStr, Prob)> {
    let data = tf.peek::<25>(0)?;

    // A bare OBU parses wholly: header, the extension octet if its flag
    // is set, and the size LEB128 if the size flag is set.
    if let Some((extension, has_size)) = obu_header(data[0]) {
        let size_at = 1 + extension as usize;
        if !has_size || leb128(&data[size_at..]).is_some() {
            return Some((Av1Format::ObuStream.as_str(), Prob::MINIMUM));
        }
    }

    // The first frame unit's length, then the first OBU's, both counted
    // from the end of the prefix before them.
    let (temporal, at) = leb128(data)?;
    let (frame, frame_prefix) = leb128(&data[at..])?;
    let at = at + frame_prefix;
    let (obu, obu_prefix) = leb128(&data[at..])?;
    let at = at + obu_prefix;

    // The delimiter header sits after the prefixes, so the prefixes end
    // inside the frame unit, and the frame unit inside the temporal unit.
    // Each length counts what follows its own prefix, which is why the
    // prefix lengths enter the bounds of the level above and the header
    // octet the bound of this one.
    // Delimiter header: forbidden and reserved zero, type 2, size flag
    // clear. Everything but the extension bit is fixed in it.
    if data[at] & 0xFB != 0x10 || obu == 0 || obu as usize + obu_prefix > frame as usize {
        return None;
    }
    let unit = frame + frame_prefix as u64;
    if unit > temporal {
        return None;
    }
    Some((Av1Format::Annexb.as_str(), Prob::MINIMUM))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::helpers::probe_engine::probe;

    /// The `f(8)` tables count bits from the most significant end, so an
    /// octet spells as that order reads it: forbidden at the top, then the
    /// four type bits, the extension flag, the size flag, and the reserved
    /// bit at the bottom.
    const TYPE_2_WITH_SIZE: u8 = 0x12; // the canonical delimiter octet
    const TYPE_2_BARE: u8 = 0x10; // delimiter, no size, no extension
    const TYPE_2_EXT: u8 = 0x14; // delimiter with an extension octet
    const TYPE_2_RESERVED: u8 = 0x13;
    const TYPE_3_BARE: u8 = 0x18; // frame header, no size: not a delimiter
    const TYPE_2_FORBIDDEN: u8 = 0x92;
    const TYPE_9: u8 = 0x4A; // reserved type
    const TYPE_1_WITH_SIZE: u8 = 0x0A; // sequence header with a size

    /// The leading octets, padded to the probe window: a shorter stream
    /// is rejected by the window, not by the conditions under test.
    fn padded(prefix: &[u8]) -> Vec<u8> {
        let mut data = prefix.to_vec();
        data.resize(25, 0x00);
        data
    }

    fn answer(data: &[u8]) -> Option<(&'static CStr, Prob)> {
        probe(data, av1_probe)
    }

    fn format_of(data: &[u8]) -> Option<&'static CStr> {
        answer(data).map(|(f, _)| f)
    }

    /// The bare framing needs one valid header and, when the size flag
    /// announces one, a terminated LEB128 behind it.
    #[test]
    fn a_bare_obu_parses_wholly() {
        assert_eq!(
            format_of(&padded(&[TYPE_2_WITH_SIZE, 0x00])),
            Some(c"obu-stream")
        );
        // No size flag: nothing to terminate, one octet settles it.
        assert_eq!(
            format_of(&padded(&[TYPE_1_WITH_SIZE & !0x02])),
            Some(c"obu-stream")
        );
    }

    /// The extension flag costs the stream one more octet, and a size that
    /// does not terminate within eight octets is not an OBU.
    #[test]
    fn an_obu_needs_the_octets_its_header_announces() {
        // Size flag with eight continuation octets: no LEB128 fits in eight.
        let mut prefix = vec![TYPE_2_WITH_SIZE];
        prefix.extend(std::iter::repeat_n(0x80, 8));
        assert_eq!(answer(&padded(&prefix)), None);
        // Seven continuations and a terminator is the longest legal size,
        // at octet nine of the window.
        let mut prefix = vec![TYPE_2_WITH_SIZE];
        prefix.extend(std::iter::repeat_n(0x80, 7));
        prefix.push(0x00);
        assert_eq!(format_of(&padded(&prefix)), Some(c"obu-stream"));
        // With the extension flag set, the size follows the extension
        // octet: the zero at octet one is the extension byte, not a size.
        let mut prefix = vec![TYPE_2_EXT | 0x02, 0x00];
        prefix.extend(std::iter::repeat_n(0x80, 8));
        assert_eq!(answer(&padded(&prefix)), None);
        assert_eq!(
            format_of(&padded(&[TYPE_2_EXT | 0x02, 0x00, 0x05])),
            Some(c"obu-stream")
        );
    }

    /// Reserved and out-of-range types, the forbidden bit and the reserved
    /// bit all fail the header itself.
    #[test]
    fn invalid_header_octets_open_nothing() {
        for b in [0u8, TYPE_9, TYPE_2_RESERVED, TYPE_2_FORBIDDEN, 0x71, 0xFF] {
            assert_eq!(answer(&padded(&[b, 0x00])), None, "octet {b:#04x}");
        }
    }

    /// The temporal-unit framing runs three LEB128 prefixes and a bare
    /// delimiter before anything else. The shortest unit the format can
    /// produce is `03 02 01 10`: one octet of unit holding a zero-byte
    /// frame unit holding the delimiter.
    #[test]
    fn an_annexb_temporal_unit_leads_with_a_delimiter() {
        assert_eq!(
            format_of(&padded(&[0x03, 0x02, 0x01, TYPE_2_BARE])),
            Some(c"annexb")
        );
        // A delimiter may carry the extension flag; the frame unit then
        // holds the extension octet too: obu length 2, frame 4, unit 5.
        assert_eq!(
            format_of(&padded(&[0x05, 0x04, 0x02, TYPE_2_EXT])),
            Some(c"annexb")
        );
        // Multi-octet prefixes: temporal 515 over two octets, frame 513,
        // obu 1, delimiter at octet five.
        assert_eq!(
            format_of(&padded(&[0x83, 0x04, 0x81, 0x04, 0x01, TYPE_2_BARE])),
            Some(c"annexb")
        );
    }

    /// The unit's lengths have to reach the delimiter and the levels have
    /// to contain each other.
    #[test]
    fn a_temporal_unit_cannot_promise_more_than_it_holds() {
        // The delimiter's header octet counted inside the OBU length
        // instead of beyond it: the unit is a whole octet short.
        assert_eq!(answer(&padded(&[0x01, 0x00, 0x00, TYPE_2_BARE])), None);
        // Zero-length OBU: the header octet it must contain is missing.
        assert_eq!(answer(&padded(&[0x02, 0x01, 0x00, TYPE_2_BARE])), None);
        // Frame unit longer than the temporal unit that holds it.
        assert_eq!(answer(&padded(&[0x02, 0x03, 0x01, TYPE_2_BARE])), None);
        // A frame unit whose size takes two octets is 130 octets long
        // with its prefix, one more than the temporal unit claims.
        assert_eq!(
            answer(&padded(&[0x81, 0x01, 0x80, 0x01, 0x01, TYPE_2_BARE])),
            None
        );
        // The same unit with room for the two-octet prefix is accepted.
        assert_eq!(
            format_of(&padded(&[0x82, 0x01, 0x80, 0x01, 0x01, TYPE_2_BARE])),
            Some(c"annexb")
        );
        // OBU longer than the frame unit that holds it.
        assert_eq!(answer(&padded(&[0x06, 0x04, 0x05, TYPE_2_BARE])), None);
        // Size flag on a delimiter: an empty OBU has nothing to size.
        assert_eq!(answer(&padded(&[0x03, 0x02, 0x01, TYPE_2_WITH_SIZE])), None);
        // Reserved type where the delimiter should be.
        assert_eq!(answer(&padded(&[0x03, 0x02, 0x01, TYPE_9])), None);
        // Forbidden-bit octet where the delimiter should be.
        assert_eq!(answer(&padded(&[0x03, 0x02, 0x01, TYPE_2_FORBIDDEN])), None);
        // Type 3 (frame header) is not the delimiter; the mask keeps the
        // type's low bit for exactly that.
        assert_eq!(answer(&padded(&[0x03, 0x02, 0x01, TYPE_3_BARE])), None);
        // A continuation-laden LEB128 prefix never terminates.
        let mut prefix = vec![0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00];
        prefix.extend([0x02, 0x01, TYPE_2_BARE]);
        assert_eq!(answer(&padded(&prefix)), None);
    }

    /// Both framings answer at minimum confidence: a header is all either
    /// reading examines, and the bare parse is tried first.
    #[test]
    fn both_framings_answer_at_minimum_confidence() {
        assert_eq!(
            answer(&padded(&[TYPE_2_WITH_SIZE, 0x00])),
            Some((c"obu-stream", Prob::MINIMUM))
        );
        assert_eq!(
            answer(&padded(&[0x03, 0x02, 0x01, TYPE_2_BARE])),
            Some((c"annexb", Prob::MINIMUM))
        );
    }

    /// The type table accepts one through eight and fifteen. The probe
    /// window decides short streams: the same octets, one short of the
    /// window, answer nothing.
    #[test]
    fn the_type_table_is_swept_at_its_edges() {
        for t in 1..=15u8 {
            let b = (t << 3) | 0x02;
            let accepted = (1..=8).contains(&t) || t == 15;
            assert_eq!(
                format_of(&padded(&[b, 0x00])).is_some(),
                accepted,
                "type {t}"
            );
        }
        assert_eq!(answer(&padded(&[TYPE_2_WITH_SIZE, 0x00])[..24]), None);
    }

    /// A size that spells four GiB or more is no size: the bare parse
    /// fails on it, and so does every prefix of the temporal-unit framing.
    #[test]
    fn a_size_beyond_four_gib_is_no_size() {
        // Four GiB exactly, in the fewest octets that spell it.
        assert_eq!(
            answer(&padded(&[TYPE_2_WITH_SIZE, 0x80, 0x80, 0x80, 0x80, 0x10])),
            None
        );
        // The largest value that is still a size: four GiB less one.
        assert_eq!(
            format_of(&padded(&[TYPE_2_WITH_SIZE, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F])),
            Some(c"obu-stream")
        );
        // The widest encoding, all payload bits set: beyond the ceiling.
        let mut prefix = vec![TYPE_2_WITH_SIZE];
        prefix.extend(std::iter::repeat_n(0xFF, 7));
        prefix.push(0x7F);
        assert_eq!(answer(&padded(&prefix)), None);
    }
}
