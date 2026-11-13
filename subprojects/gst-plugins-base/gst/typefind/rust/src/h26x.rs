// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Probes for the H.26x family of elementary video streams. Each is a run of
//! syntactic elements, each introduced by a start code, and what a finder has
//! to settle is whether the octets behind a start code could only have come
//! from that codec. The three NAL-based codecs introduce each unit with the
//! `00 00 01` prefix; H.263 syncs instead on a 22-bit picture start code that
//! is a bit pattern inside a byte-aligned word, so it scans for octets rather
//! than for a prefix.
//!
//! The three NAL-based codecs differ in where the NAL unit type sits inside the
//! first two octets after the start code, which is why each reads its own
//! fields rather than sharing one mask.

use crate::helpers::{Prob, Scanner, TypeFind};
use crate::scan::Pattern;
use crate::startcode::next_start_code;

/// How far a finder looks into the stream. The parameter sets and the first
/// pictures of an HD stream fit well inside this.
const PROBE_LIMIT: i64 = 128 * 1024;

/// What one NAL header says about the stream.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Nal {
    /// A header the format cannot produce. Nothing further in the stream makes
    /// the format likely again, so the walk stops.
    Stop,
    /// A type the format does not treat as its own: reserved, or defined but
    /// outside what a finder counts. Counted against the stream, but the walk
    /// goes on: one such NAL does not rule the format out.
    Bad,
    /// A type the format defines, with the structures it names.
    Good(u8),
    /// A type the format leaves to applications, which is evidence about
    /// nothing.
    Silent,
}

/// The structures a NAL header can name. Each finder reads the ones its own
/// type table defines.
const SPS: u8 = 1 << 0;
const PPS: u8 = 1 << 1;
const VPS: u8 = 1 << 2;
const IDR: u8 = 1 << 3;
const IRAP: u8 = 1 << 4;
const SSPS: u8 = 1 << 5;

/// H.264 7.3.1 lays the header out as forbidden_zero_bit(1), nal_ref_idc(2),
/// nal_unit_type(5). The mask keeps the forbidden bit directly above the five
/// bit type, so a set forbidden bit lifts the value past every type Table 7-1
/// defines, which is what the `> 0x1f` test below reads. `seen_ssps` says
/// whether a subset sequence parameter set has been seen.
fn h264_nal(header: u8, seen_ssps: bool) -> Nal {
    let nut = header & 0x9f;
    let ref_idc = header & 0x60;
    if nut > 0x1f {
        return Nal::Stop;
    }
    match nut {
        // Table 7-1: 0 is unspecified and left to the application.
        0 => Nal::Silent,
        1..=13 | 19 => {
            // 7.4.1: nal_ref_idc is required for an IDR slice and forbidden
            // for SEI, access unit delimiter, end of sequence, end of stream
            // and filler data.
            if (nut == 5 && ref_idc == 0) || ((nut == 6 || (9..=12).contains(&nut)) && ref_idc != 0)
            {
                Nal::Bad
            } else {
                Nal::Good(match nut {
                    5 => IDR,
                    7 => SPS,
                    8 => PPS,
                    _ => 0,
                })
            }
        }
        // Table 7-1: 15 is the subset sequence parameter set.
        15 => Nal::Good(SSPS),
        // shortcut: a prefix NAL unit (14) or a coded slice extension (20) is
        // counted only once a subset sequence parameter set has been seen,
        // which is what a stream entered in mid-picture lacks.
        14 | 20 => {
            if seen_ssps {
                Nal::Good(0)
            } else {
                Nal::Silent
            }
        }
        // shortcut: Table 7-1 also defines 16 and 21 and leaves 24 to 31 to
        // the application, and those are counted against the stream here; it
        // matters for a stream that carries a depth extension or an
        // application-defined NAL unit.
        _ => Nal::Bad,
    }
}

/// H.265 7.3.1.2 lays the header out as forbidden_zero_bit(1),
/// nal_unit_type(6), nuh_layer_id(6), nuh_temporal_id_plus1(3), so the type is
/// the six bits under the forbidden bit of the first octet and the layer id
/// runs over into the second octet. Table 7-1 names the types.
fn h265_nal(header: &[u8; 2]) -> Nal {
    let nut = header[0] & 0xfe;
    if nut > 0x7e {
        return Nal::Stop;
    }
    let nut = nut >> 1;
    // shortcut: only the base layer is accepted, which matters for a scalable
    // stream, whose enhancement-layer NAL units carry a nonzero nuh_layer_id.
    if header[0] & 0x01 != 0 || header[1] & 0xf8 != 0 {
        return Nal::Stop;
    }
    // 7.4.2.2: nuh_temporal_id_plus1 is never zero.
    if header[1] & 0x07 == 0 {
        return Nal::Stop;
    }
    match nut {
        0..=9 | 16..=21 | 32..=40 => Nal::Good(match nut {
            32 => VPS,
            33 => SPS,
            34 => PPS,
            // Table 7-1: 16 to 21 are the BLA, IDR and CRA pictures.
            16..=21 => IRAP,
            _ => 0,
        }),
        // Table 7-1: 10 to 15, 22 to 31 and 41 to 47 are reserved.
        10..=15 | 22..=31 | 41..=47 => Nal::Bad,
        // Table 7-1: 48 to 63 are unspecified.
        _ => Nal::Silent,
    }
}

/// H.266 7.3.1.2 lays the header out as forbidden_zero_bit(1),
/// nuh_reserved_zero_bit(1), nuh_layer_id(6), then nal_unit_type(5) and
/// nuh_temporal_id_plus1(3), so the type is the five top bits of the second
/// octet, not of the first. Table 5 names the types.
fn h266_nal(header: &[u8; 2]) -> Nal {
    // 7.4.2.2: forbidden_zero_bit and nuh_reserved_zero_bit are both zero.
    if header[0] & 0xc0 != 0 {
        return Nal::Stop;
    }
    // 7.4.2.2: nuh_temporal_id_plus1 is never zero.
    if header[1] & 0x07 == 0 {
        return Nal::Stop;
    }
    let nut = header[1] >> 3;
    // shortcut: the accepted set is 0 to 27, which counts the reserved types
    // 4 to 6, 11 and 26 to 27 for the format and the application types 28 to
    // 31 against it; it matters for a stream that carries either group.
    match nut {
        0..=27 => Nal::Good(match nut {
            14 => VPS,
            15 => SPS,
            16 => PPS,
            // Table 5: 7 to 9 are the IDR and CRA pictures.
            7..=9 => IRAP,
            _ => 0,
        }),
        _ => Nal::Bad,
    }
}

/// Whether the octets at `w` open with the picture start code of H.263 5.1.1,
/// and whether the fields read against it agree. The code is a byte aligned
/// 22-bit word whose value is sixteen zero bits, a one, then five zero bits,
/// so it lands as two zero octets followed by an octet whose top six bits are
/// `100000`.
///
/// The temporal reference of 5.1.2 is eight bits wide, so PTYPE begins two
/// bits into the fourth octet and thirteen bits run from there. The lead-in
/// is "10", the source format is sub-QCIF through 16CIF, and 5.1.3 adds
/// that an INTRA picture (bit 9 clear) shall leave the PB-frames mode of
/// Annex G (bit 13) clear.
fn h263_picture(w: &[u8; 6]) -> Option<bool> {
    if w[0] != 0 || w[1] != 0 || w[2] & 0xfc != 0x80 {
        return None;
    }
    let ptype = ((w[3] & 0x03) << 6) | (w[4] >> 2);
    let format = ptype & 0x07;
    // 5.1.3: the first two bits of PTYPE are "10", and the source format is
    // one of sub-QCIF, QCIF, CIF, 4CIF or 16CIF.
    if ptype >> 6 != 2 || format == 0 || format > 5 {
        return Some(false);
    }
    // 5.1.3 again: bit 9 is the second bit of the fifth octet, bit 13 the
    // third bit of the sixth.
    Some(w[4] & 0x02 != 0 || w[5] & 0x20 == 0)
}

/// The picture start code, 22 bits: `0x000080`.
const H263_PSC: Pattern<3> = Pattern::new([0x00, 0x00, 0x80], [0xFF, 0xFF, 0xFC]);

/// H.263 is SECONDARY, so it keeps the lowest confidence of the family and
/// leaves the bytes to anything more specific that also claims them.
pub fn h263_probe(tf: &mut TypeFind) -> Option<Prob> {
    let mut scan = Scanner::new(tf, 0);
    let mut good = 0;
    let mut bad = 0;
    // Each step reads from the first octet of a candidate picture start
    // code and stops when the six octets the fields live in no longer fit.
    // The old typefinder reaches one candidate further at each tail and
    // asks for two more octets there than it ever reads; one candidate in
    // 128 KiB is not worth a window the fields cannot explain.
    while scan.find(PROBE_LIMIT - 5, &[H263_PSC]) {
        let Some(head) = scan.peek(6) else { break };
        // `peek` answers with exactly the octets it was asked for or with
        // nothing, so the six are there to name.
        let w: &[u8; 6] = head.try_into().unwrap();
        match h263_picture(w) {
            Some(true) => good += 1,
            Some(false) => bad += 1,
            None => {}
        }
        scan.consume(1);
    }
    // A picture start code that fails its own checks is worth two of the ones
    // that pass, so a stream that is mostly broken syncs is not this format.
    (good > 2 * bad).then_some(Prob::POSSIBLE)
}

/// H.264 Annex B byte stream: NAL units, each behind a start code. The stream
/// is settled once both parameter sets and an IDR picture have turned up with
/// enough other NAL units around them; short of that, a run of headers that
/// the format allows is enough on its own.
pub fn h264_probe(tf: &mut TypeFind) -> Option<Prob> {
    let mut scan = Scanner::new(tf, 0);
    let mut good = 0;
    let mut bad = 0;
    let mut seen = 0u8;

    while scan.offset() < PROBE_LIMIT {
        // A start code beginning on the last octet of the probe is still
        // inside it, so the search may step that far.
        let room = (PROBE_LIMIT - 1 - scan.offset()) as usize;
        if next_start_code(&mut scan, room).is_none() {
            break;
        }
        let Some(head) = scan.peek(1) else { break };
        match h264_nal(head[0], seen & SSPS != 0) {
            Nal::Stop => {
                bad += 1;
                break;
            }
            Nal::Bad => bad += 1,
            Nal::Good(flags) => {
                good += 1;
                seen |= flags;
            }
            Nal::Silent => {}
        }
        if seen & (SPS | PPS | IDR) == (SPS | PPS | IDR) && good >= 10 && bad < 4 {
            return Some(Prob::LIKELY);
        }
        // The next search resumes one octet into the payload of the unit just
        // read, not at its first payload octet.
        scan.consume(2);
    }

    if good < 2 || bad != 0 {
        return None;
    }
    Some(if seen & (SPS | PPS) == (SPS | PPS) {
        Prob::LIKELY
    } else {
        Prob::POSSIBLE
    })
}

/// H.265 Annex B byte stream, with the same shape as H.264 and HEVC's own type
/// table. The parameter sets are VPS, SPS and PPS, and an IRAP picture stands
/// in for the IDR one.
pub fn h265_probe(tf: &mut TypeFind) -> Option<Prob> {
    let mut scan = Scanner::new(tf, 0);
    let mut good = 0;
    let mut bad = 0;
    let mut seen = 0u8;

    while scan.offset() < PROBE_LIMIT {
        // A start code beginning on the last octet of the probe is still
        // inside it, so the search may step that far.
        let room = (PROBE_LIMIT - 1 - scan.offset()) as usize;
        let Some(_) = next_start_code(&mut scan, room) else {
            break;
        };
        // The header is two octets, and a start code with no header left in
        // front of it ends the search.
        let Some(head) = scan.peek(2) else { break };
        let head = [head[0], head[1]];
        match h265_nal(&head) {
            Nal::Stop => {
                bad += 1;
                break;
            }
            Nal::Bad => bad += 1,
            Nal::Good(flags) => {
                good += 1;
                seen |= flags;
            }
            Nal::Silent => {}
        }
        if seen & (SPS | PPS | IRAP) == (SPS | PPS | IRAP) && good >= 10 && bad < 4 {
            return Some(Prob::LIKELY);
        }
        // The next search resumes past the two-octet header and one
        // octet of the payload that follows it.
        scan.consume(3);
    }

    if good < 2 || bad != 0 {
        return None;
    }
    Some(if seen & (VPS | SPS | PPS) == (VPS | SPS | PPS) {
        Prob::LIKELY
    } else {
        Prob::POSSIBLE
    })
}

/// H.266 Annex B byte stream, again the same shape with VVC's own type table.
pub fn h266_probe(tf: &mut TypeFind) -> Option<Prob> {
    let mut scan = Scanner::new(tf, 0);
    let mut good = 0;
    let mut bad = 0;
    let mut seen = 0u8;

    while scan.offset() < PROBE_LIMIT {
        // A start code beginning on the last octet of the probe is still
        // inside it, so the search may step that far.
        let room = (PROBE_LIMIT - 1 - scan.offset()) as usize;
        let Some(_) = next_start_code(&mut scan, room) else {
            break;
        };
        let Some(head) = scan.peek(2) else { break };
        let head = [head[0], head[1]];
        match h266_nal(&head) {
            Nal::Stop => {
                bad += 1;
                break;
            }
            Nal::Bad => bad += 1,
            Nal::Good(flags) => {
                good += 1;
                seen |= flags;
            }
            Nal::Silent => {}
        }
        if seen & (SPS | PPS | IRAP) == (SPS | PPS | IRAP) && good >= 10 && bad < 4 {
            return Some(Prob::LIKELY);
        }
        // The next search resumes past the two-octet header and one
        // octet of the payload that follows it.
        scan.consume(3);
    }

    // shortcut: the weakest rung stays weak on purpose. Two headers the
    // table cannot rule out and no parameter set seen are enough for a
    // POSSIBLE at 50%, so data merely shaped like VVC claims it — a JPEG
    // behind one leading zero octet does. H.264 and H.265 share the rung;
    // tightening it would change the family's decision rule, not VVC's
    // type table.
    if good < 2 || bad != 0 {
        return None;
    }
    Some(if seen & (VPS | SPS | PPS) == (VPS | SPS | PPS) {
        Prob::LIKELY
    } else {
        Prob::POSSIBLE
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// H.264 7.3.1: forbidden_zero_bit(1) | nal_ref_idc(2) | nal_unit_type(5).
    /// Values come from Table 7-1. The parameter sets below carry
    /// nal_ref_idc 0, which 7.4.1 forbids for them; a header is judged on its
    /// type, and only the types with a rule attached are held to it.
    #[test]
    fn h264_headers_sort_as_the_type_table_does() {
        for (header, want) in [
            (0x00, Nal::Silent),     // 0, unspecified
            (0x01, Nal::Good(0)),    // 1, slice of a non-IDR picture
            (0x06, Nal::Good(0)),    // 6, SEI, nal_ref_idc 0
            (0x26, Nal::Bad),        // 6, SEI with nal_ref_idc 1
            (0x07, Nal::Good(SPS)),  // 7, sequence parameter set
            (0x08, Nal::Good(PPS)),  // 8, picture parameter set
            (0x09, Nal::Good(0)),    // 9, access unit delimiter
            (0x29, Nal::Bad),        // 9, with nal_ref_idc 1
            (0x0c, Nal::Good(0)),    // 12, filler data
            (0x0d, Nal::Good(0)),    // 13, sequence parameter set ext
            (0x13, Nal::Good(0)),    // 19, auxiliary coded picture
            (0x05, Nal::Bad),        // 5, IDR with nal_ref_idc 0
            (0x65, Nal::Good(IDR)),  // 5, IDR with nal_ref_idc 3
            (0x0f, Nal::Good(SSPS)), // 15, subset sequence param set
            (0x10, Nal::Bad),        // 16, depth parameter set
            (0x18, Nal::Bad),        // 24, unspecified
            (0x1f, Nal::Bad),        // 31, unspecified
            (0x80, Nal::Stop),       // forbidden bit set, type 0
            (0xe7, Nal::Stop),       // forbidden bit set, type 7
        ] {
            assert_eq!(h264_nal(header, false), want, "header {header:#04x}");
        }
    }

    /// H.264 Table 7-1: a prefix NAL unit (14) and a coded slice extension (20)
    /// are only counted once a subset sequence parameter set (15) has been
    /// seen, which is what a stream entered in mid-picture lacks.
    #[test]
    fn h264_counts_prefix_and_extension_units_only_after_a_subset_sps() {
        for type_code in [0x0eu8, 0x14] {
            assert_eq!(h264_nal(type_code, false), Nal::Silent);
            assert_eq!(h264_nal(type_code, true), Nal::Good(0));
        }
    }

    /// H.265 7.3.1.2: forbidden_zero_bit(1) | nal_unit_type(6) |
    /// nuh_layer_id(6) | nuh_temporal_id_plus1(3), so the type is the first
    /// octet shifted one place right and 0x01 is layer 0 with temporal id 1.
    /// Values come from Table 7-1.
    #[test]
    fn h265_headers_sort_as_the_type_table_does() {
        for (nut, want) in [
            (0, Nal::Good(0)),     // 0, TRAIL_N
            (9, Nal::Good(0)),     // 9, RASL_R
            (10, Nal::Bad),        // 10, RSV_VCL_N10
            (15, Nal::Bad),        // 15, RSV_VCL_R15
            (16, Nal::Good(IRAP)), // 16, BLA_W_LP
            (21, Nal::Good(IRAP)), // 21, CRA_NUT
            (22, Nal::Bad),        // 22, RSV_IRAP_VCL22
            (31, Nal::Bad),        // 31, RSV_VCL31
            (32, Nal::Good(VPS)),  // 32, VPS_NUT
            (33, Nal::Good(SPS)),  // 33, SPS_NUT
            (34, Nal::Good(PPS)),  // 34, PPS_NUT
            (40, Nal::Good(0)),    // 40, SUFFIX_SEI_NUT
            (41, Nal::Bad),        // 41, RSV_NVCL41
            (47, Nal::Bad),        // 47, RSV_NVCL47
            (48, Nal::Silent),     // 48, UNSPEC48
            (63, Nal::Silent),     // 63, UNSPEC63
        ] {
            let header = [((nut << 1) & 0xfe) as u8, 0x01];
            assert_eq!(h265_nal(&header), want, "nal_unit_type {nut}");
        }
    }

    /// H.265 7.3.1.2 and 7.4.2.1: the forbidden bit is zero, nuh_layer_id is
    /// zero, and nuh_temporal_id_plus1 is not. Each of the three ends the walk.
    #[test]
    fn h265_rejects_headers_it_cannot_contain() {
        // forbidden_zero_bit set on a VPS.
        assert_eq!(h265_nal(&[0xc0, 0x01]), Nal::Stop);
        // nuh_layer_id has its top bit set, which is the last bit of octet 0.
        assert_eq!(h265_nal(&[0x41, 0x01]), Nal::Stop);
        // nuh_layer_id has a lower bit set, in octet 1.
        assert_eq!(h265_nal(&[0x40, 0x09]), Nal::Stop);
        // nuh_temporal_id_plus1 of zero.
        assert_eq!(h265_nal(&[0x40, 0x00]), Nal::Stop);
    }

    /// H.266 7.3.1.2: forbidden_zero_bit(1) | nuh_reserved_zero_bit(1) |
    /// nuh_layer_id(6) | nal_unit_type(5) | nuh_temporal_id_plus1(3), so the
    /// type is the top five bits of the second octet. Values come from Table 5.
    #[test]
    fn h266_headers_sort_as_the_type_table_does() {
        for (nut, want) in [
            (0, Nal::Good(0)),    // 0, TRAIL_NUT
            (4, Nal::Good(0)),    // 4, RSV_VCL_4
            (7, Nal::Good(IRAP)), // 7, IDR_W_RADL
            (9, Nal::Good(IRAP)), // 9, CRA_NUT
            (14, Nal::Good(VPS)), // 14, VPS_NUT
            (15, Nal::Good(SPS)), // 15, SPS_NUT
            (16, Nal::Good(PPS)), // 16, PPS_NUT
            (26, Nal::Good(0)),   // 26, RSV_NVCL_26
            (27, Nal::Good(0)),   // 27, RSV_NVCL_27
            (28, Nal::Bad),       // 28, UNSPEC_28
            (31, Nal::Bad),       // 31, UNSPEC_31
        ] {
            let header = [0x00, ((nut << 3) | 1) as u8];
            assert_eq!(h266_nal(&header), want, "nal_unit_type {nut}");
        }
    }

    /// H.266 7.4.2.2: forbidden_zero_bit, nuh_reserved_zero_bit and
    /// nuh_temporal_id_plus1 are constrained, and each ends the walk.
    #[test]
    fn h266_rejects_headers_it_cannot_contain() {
        // forbidden_zero_bit set on a VPS.
        assert_eq!(h266_nal(&[0x80, 0x71]), Nal::Stop);
        // nuh_reserved_zero_bit set.
        assert_eq!(h266_nal(&[0x40, 0x71]), Nal::Stop);
        // nuh_temporal_id_plus1 of zero.
        assert_eq!(h266_nal(&[0x00, 0x70]), Nal::Stop);
    }

    /// H.263 5.1.1: the picture start code is sixteen zero bits, a one, then
    /// five zero bits, and it is byte aligned, so it lands as two zero octets
    /// and an octet whose top six bits are `100000`. The eight bits checked
    /// against it have to start "10" and carry a source format of 1 to 5,
    /// which is sub-QCIF through 16CIF (5.1.3).
    #[test]
    fn h263_picture_start_code_and_its_fields_are_found() {
        // The eight bits read are 0b10_000_001: "10" on top, source format 1.
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x04, 0x00]),
            Some(true)
        );
        // The pattern has to be exactly `100000` across the top six bits, so a
        // stray bit anywhere inside that window is not a picture start code.
        assert_eq!(h263_picture(&[0x00, 0x00, 0x84, 0x0a, 0x04, 0x00]), None);
        // A source format of zero is forbidden, six is reserved, and seven is
        // the extended form of 5.1.3, which is not counted as this format.
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x00, 0x00]),
            Some(false)
        );
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x18, 0x00]),
            Some(false)
        );
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x1c, 0x00]),
            Some(false)
        );
        // An INTRA picture (bit 9 clear) may not claim PB-frames mode,
        // which sits at bit 13: the third bit of the sixth octet.
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x04, 0x20]),
            Some(false)
        );
        assert_eq!(
            h263_picture(&[0x00, 0x00, 0x82, 0x0a, 0x06, 0x20]),
            Some(true)
        );
        // Anything that is not the picture start code is not a candidate.
        assert_eq!(h263_picture(&[0x00, 0x00, 0x00, 0x0a, 0x04, 0x00]), None);
        assert_eq!(h263_picture(&[0x00, 0x01, 0x82, 0x0a, 0x04, 0x00]), None);
    }
}
