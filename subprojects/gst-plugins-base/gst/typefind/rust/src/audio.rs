// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Probes for the audio formats that are a run of self-describing frames.
//! Each probe decides what the stream is; the typefinders only render the
//! decision.

use crate::helpers::{
    Prob, Scanner, TypeFind, aac_channels, aac_level, aac_profile, aac_sample_rate, u16be, u16le,
    u32be,
};
use std::ffi::CStr;

// One registration covers three framings of the same codec. The frame layouts
// are 13818-7 Tables 8 and 9, which 14496-3 Tables 1.A.6 and 1.A.7 repeat
// field for field, for ADTS, and 14496-3 Tables 1.36 and 1.37 for LOAS.

/// A stream may start anywhere in here: files with junk in front exist.
const AAC_WINDOW: i64 = 4096;

/// What the probe decided: the framing and everything the suggestion needs.
/// Deciding here keeps the confidence next to the evidence that earned it and
/// leaves the typefinder as a rendering step.
pub enum AacFraming {
    Adif,
    Adts {
        probability: u32,
        mpegversion: i32,
        /// Speaker count and sampling frequency. Absent when
        /// `channel_configuration` is 0 and the layout is left to a
        /// program_config_element, and without a layout there is no level.
        layout: Option<(i32, i32)>,
        level: Option<&'static CStr>,
        profile: &'static CStr,
    },
    Loas {
        probability: u32,
    },
}

/// Frames from the cursor on, each located by the header before it. `limit`
/// caps the count and `header_len` is how much of a header `frame_len` needs,
/// which for LOAS is less than a frame can be, so asking for more would end
/// the chain early. `frame_len` reads a header and says how far it reaches;
/// the chain ends at the first header it does not recognise, or at a zero
/// length.
fn count_frames(
    scanner: &mut Scanner,
    limit: usize,
    header_len: usize,
    frame_len: impl Fn(&[u8]) -> Option<usize>,
) -> usize {
    let mut frames = 0;
    while frames < limit {
        let Some(header) = scanner.peek(header_len) else {
            break;
        };
        let Some(len) = frame_len(header) else { break };
        if len == 0 {
            break;
        }
        scanner.consume(len);
        frames += 1;
    }
    frames
}

/// ADTS opens with a 12-bit sync and a zero layer field, 13818-7 Table 8. Those
/// are the bits an MPEG audio layer would occupy, and code 0 names no layer, so
/// no ADTS frame can read as an mp3 header. The ID bit is not part of the test,
/// so one probe answers for both versions.
fn is_adts(head: &[u8]) -> bool {
    u16be(head) & 0xFFF6 == 0xFFF0
}

/// The frame_length field, split 2, 8 and 3 bits across octets 3 to 5,
/// 13818-7 Table 8.
fn adts_frame_len(head: &[u8]) -> Option<usize> {
    is_adts(head).then(|| {
        ((head[3] & 0x03) as usize) << 11
            | (head[4] as usize) << 3
            | ((head[5] & 0xE0) >> 5) as usize
    })
}

/// The two octets of AudioSpecificConfig that ADTS spells out for itself:
/// objectType of 5, samplingFrequencyIndex of 4 and channelConfiguration of 4,
/// 14496-3 1.6.4.1. ADTS numbers the object type from zero.
fn adts_config(object_type: u8, freq_index: u8, chan_config: u8) -> [u8; 2] {
    [
        object_type << 3 | freq_index >> 1,
        (freq_index & 1) << 7 | chan_config << 3,
    ]
}

/// The error-protected mux element sync, 14496-3 Table 1.37.
fn is_loas_ep(head: &[u8]) -> bool {
    u16be(head) == 0x4DE1
}

/// audioMuxLengthBytes counts the payload only, so the seven-octet header is
/// added back, 14496-3 Table 1.37. A zero length carries no audio.
fn loas_ep_frame_len(head: &[u8]) -> Option<usize> {
    if !is_loas_ep(head) {
        return None;
    }
    let payload =
        ((head[2] & 0x0F) as usize) << 9 | (head[3] as usize) << 1 | (head[4] >> 7) as usize;
    (payload > 0).then(|| 7 + payload)
}

/// The equal-payload sync is eleven bits, 14496-3 Table 1.36, so the top of the
/// length shares the second octet.
fn is_loas(head: &[u8]) -> bool {
    u16be(head) & 0xFFE0 == 0x56E0
}

/// audioMuxLengthBytes for an equal-payload stream, 14496-3 Table 1.36. The
/// header is three octets and is already inside the length.
fn loas_frame_len(head: &[u8]) -> Option<usize> {
    if !is_loas(head) {
        return None;
    }
    let payload = ((head[1] & 0x1F) as usize) << 8 | head[2] as usize;
    (payload > 0).then(|| 3 + payload)
}

/// The first candidate whose chain verifies. ADTS wins outright; otherwise the
/// longest LOAS chain wins. The three sync patterns are mutually exclusive, so
/// a candidate is never ambiguous. A candidate that does not chain only rules
/// out its own offset, so the probe moves one byte and tries again; an
/// exhausted window is the only answer of none.
pub fn aac_probe(tf: &mut TypeFind) -> Option<AacFraming> {
    // The ADIF header is defined to sit at the start of the sequence.
    if tf.starts_with(b"ADIF") {
        return Some(AacFraming::Adif);
    }

    let mut scanner = Scanner::new(tf, 0);
    let mut best_loas = 0;
    loop {
        let start = scanner.offset();
        if start >= AAC_WINDOW {
            break;
        }
        let mut probe = scanner;
        let Some(head) = probe.peek(6) else { break };
        // A chain walk runs as far as the frames reach, which says nothing about
        // where the next candidate starts. A copy of the cursor walks; the scan
        // stays on the byte it is trying.
        if is_adts(head) {
            let mpegversion = if head[1] & 0x08 != 0 { 2 } else { 4 };
            let object_type = (head[2] >> 6) + 1;
            let freq_index = (head[2] & 0x3C) >> 2;
            let chan_config = ((head[2] & 1) << 2) | (head[3] >> 6);
            let frames = count_frames(&mut probe, 7, 6, adts_frame_len);
            // 13818-7 Table 35 reserves 0xc..0xf, while 14496-3 Table 1.18
            // defines 0xc as 7350 and reserves 0xd and 0xe, with 0xf an escape
            // value that 1.A.4.3 does not permit here at all. One bound of 12
            // serves either version.
            if frames >= 2 && freq_index <= 12 {
                // Two frames claim the format; the ladder only starts moving
                // once the chain runs past three, and tops out at seven.
                let asc = adts_config(object_type, freq_index, chan_config);
                // Cannot happen: ADTS carries object types 1..4 and every one
                // of them has a profile. Kept because aac_profile can answer
                // none, and guessing a profile would be worse than not claiming
                // the stream.
                let profile = aac_profile(&asc)?;
                // A channel configuration of 0 defers the layout to a
                // program_config_element and reports no channels, and without
                // a layout there is no level to name either.
                let channels = aac_channels(&asc);
                let layout = (channels > 0).then(|| {
                    let rate = aac_sample_rate(&asc);
                    (channels as i32, rate as i32)
                });
                return Some(AacFraming::Adts {
                    probability: 80 + 5 * frames.saturating_sub(3) as u32,
                    mpegversion,
                    level: layout.and_then(|_| aac_level(&asc)),
                    layout,
                    profile,
                });
            }
        } else {
            // LOAS. A stream keeps to one framing, so the first header fixes
            // which chain is walked.
            let frames = if is_loas_ep(head) {
                count_frames(&mut probe, 21, 5, loas_ep_frame_len)
            } else if is_loas(head) {
                count_frames(&mut probe, 21, 3, loas_frame_len)
            } else {
                0
            };
            if frames >= 4 && frames > best_loas {
                best_loas = frames;
                // Fifteen frames reach LIKELY, past which scanning on cannot
                // change the outcome.
                if best_loas >= 15 {
                    break;
                }
            }
        }
        // A header that does not chain is a false positive from junk ahead of
        // the stream, not a verdict on the file. The real header may start at
        // the next byte, so try that.
        scanner.consume(1);
    }
    if best_loas == 0 {
        return None;
    }
    // Four frames claim the format; each further frame raises confidence up
    // to twenty-one.
    Some(AacFraming::Loas {
        probability: 40 + 3 * (best_loas - 1) as u32,
    })
}

// ---------------------------------------------------------------------------
// DTS
// ---------------------------------------------------------------------------

/// A frame is never shorter than the shortest one the format allows, so a
/// header with less than that behind it is a fragment rather than a frame.
const DTS_MIN_FRAME: usize = 96;

/// How far into a file the first frame may hide: the largest frame the format
/// can describe, so that a first frame anywhere within reach of that is found.
const DTS_WINDOW: i64 = 18725;

/// Speaker counts by channel index, 102-114 Table 5-1.
const DTS_CHANNELS: [i32; 16] = [1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8];

/// Sampling frequencies by sampling frequency index, 102-114 Table 5-1. Zero
/// marks the indices the format leaves unassigned.
const DTS_RATES: [i32; 16] = [
    0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0, 12000, 24000, 48000, 96000, 192000,
];

/// What the DTS probe decided.
pub struct DtsFrame {
    pub probability: u32,
    pub rate: i32,
    /// Zero when the channel index is one the format leaves unassigned and the
    /// layout cannot be told.
    pub channels: i32,
    pub depth: i32,
    pub endianness: i32,
}

/// The four sync markers of 102-114 Figure 5-1 carry the word depth and the
/// byte order as well as the sync, so the marker alone says how the rest of the
/// header has to be read.
#[derive(Clone, Copy)]
enum DtsMarker {
    Be16,
    Be14,
    Le16,
    Le14,
}

impl DtsMarker {
    fn new(marker: u32) -> Option<Self> {
        match marker {
            0x7FFE_8001 => Some(Self::Be16),
            0x1FFF_E800 => Some(Self::Be14),
            0xFE7F_0180 => Some(Self::Le16),
            0xFF1F_00E8 => Some(Self::Le14),
            _ => None,
        }
    }

    /// The header as eight words in the byte order the marker states. The
    /// 14-bit forms carry the same fields in words six bits shorter, so each
    /// word pulls its missing low bits off the top of the next one; only the
    /// words the fields live in need pulling. `None` when a 14-bit marker is
    /// not followed by the word the format reserves there.
    fn words(self, d: &[u8]) -> Option<[u16; 8]> {
        let read = if matches!(self, Self::Be16 | Self::Be14) {
            u16be
        } else {
            u16le
        };
        let mut w: [u16; 8] = std::array::from_fn(|i| read(&d[2 * i..]));
        if self.packed() {
            if w[2] & 0xFFF0 != 0x07F0 {
                return None;
            }
            // Each word pulls the low bits it is short from the top of the next
            // word's fourteen data bits, the two void bits above them
            // discarded.
            for i in 2..6 {
                let s = 2 * (i + 1);
                w[i] = (w[i] << s) | ((w[i + 1] >> (14 - s)) & ((1 << s) - 1));
            }
        }
        Some(w)
    }

    fn packed(self) -> bool {
        matches!(self, Self::Be14 | Self::Le14)
    }

    fn depth(self) -> i32 {
        match self {
            Self::Be14 | Self::Le14 => 14,
            Self::Be16 | Self::Le16 => 16,
        }
    }

    fn endianness(self) -> i32 {
        match self {
            Self::Be14 | Self::Be16 => 4321,
            Self::Le14 | Self::Le16 => 1234,
        }
    }
}

/// Where the frame at the cursor reaches, and what it says about the audio
/// behind it. `None` when the header describes no frame the format allows.
fn dts_header(kind: DtsMarker, d: &[u8]) -> Option<(usize, i32, i32)> {
    let w = kind.words(d)?;
    let blocks = (w[2] >> 2) & 0x7F;
    let size = ((((w[2] & 3) as usize) << 12) | (w[3] >> 4) as usize) + 1;
    let rate = DTS_RATES[((w[4] >> 10) & 0x0F) as usize];
    if blocks < 5 || size < DTS_MIN_FRAME || rate == 0 {
        return None;
    }
    // The length field counts bytes of the unpacked frame. The packed form
    // carries 14 bits in every 16, so the frame on disk is longer by that.
    let size = if kind.packed() { size * 16 / 14 } else { size };
    let index = (((w[3] & 0x0F) << 2) | (w[4] >> 14)) as usize;
    // Only zero means no subwoofer, so the two reserved values count as one.
    let lfe = i32::from((w[5] >> 9) & 0x03 != 0);
    let channels = DTS_CHANNELS.get(index).map_or(0, |n| n + lfe);
    Some((size, rate, channels))
}

pub fn dts_probe(tf: &mut TypeFind) -> Option<DtsFrame> {
    let mut scan = Scanner::new(tf, 0);
    while scan.offset() <= DTS_WINDOW {
        let start = scan.offset();
        // A header that leads nowhere must not drag the scan with it, so all of
        // the speculative reading happens on a copy of the cursor.
        let mut probe = scan;
        let Some(head) = probe.peek(DTS_MIN_FRAME) else {
            break;
        };
        let marker = u32be(head);
        let Some(kind) = DtsMarker::new(marker) else {
            scan.consume(1);
            continue;
        };
        let Some((size, rate, channels)) = dts_header(kind, head) else {
            scan.consume(1);
            continue;
        };
        // A second frame starting where this one ends is what turns a header
        // that merely parses into a stream.
        probe.consume(size);
        let again = probe.peek(4).is_some_and(|next| u32be(next) == marker);
        return Some(DtsFrame {
            probability: if again {
                Prob::MAXIMUM.get()
            } else if start == 0 {
                Prob::LIKELY.get()
            } else {
                Prob::POSSIBLE.get()
            },
            rate,
            channels,
            depth: kind.depth(),
            endianness: kind.endianness(),
        });
    }
    None
}

// ---------------------------------------------------------------------------
// AC-3 and E-AC-3
// ---------------------------------------------------------------------------

/// Frame size in bytes, by `frmsizecod` then by `fscod`, which runs 48, 44.1
/// and 32 kHz in that order. A/52 table 5-15 gives the sizes in 16-bit words;
/// these are the byte counts.
const AC3_FRAME_SIZE: [[usize; 3]; 38] = [
    [64, 69, 96],
    [64, 70, 96],
    [80, 87, 120],
    [80, 88, 120],
    [96, 104, 144],
    [96, 105, 144],
    [112, 121, 168],
    [112, 122, 168],
    [128, 139, 192],
    [128, 140, 192],
    [160, 174, 240],
    [160, 175, 240],
    [192, 208, 288],
    [192, 209, 288],
    [224, 243, 336],
    [224, 244, 336],
    [256, 278, 384],
    [256, 279, 384],
    [320, 348, 480],
    [320, 349, 480],
    [384, 417, 576],
    [384, 418, 576],
    [448, 487, 672],
    [448, 488, 672],
    [512, 557, 768],
    [512, 558, 768],
    [640, 696, 960],
    [640, 697, 960],
    [768, 835, 1152],
    [768, 836, 1152],
    [896, 975, 1344],
    [896, 976, 1344],
    [1024, 1114, 1536],
    [1024, 1115, 1536],
    [1152, 1253, 1728],
    [1152, 1254, 1728],
    [1280, 1393, 1920],
    [1280, 1394, 1920],
];

/// How far into a file the first frame may hide.
const AC3_WINDOW: i64 = 1024;

/// Which framing a sync belongs to. The media type is the only difference, and
/// `bsid` says which because A/52 leaves the values in between unassigned.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Ac3Stream {
    Ac3,
    EAc3,
}

/// The frame header of A/52 5.4.2: the sync, then `bsid` to tell the two
/// framings apart, then however that framing spells its frame length. `None`
/// when the header is not one, including the `bsid` values A/52 leaves
/// unassigned and a geometry outside the tables.
fn ac3_header(d: &[u8]) -> Option<(Ac3Stream, usize)> {
    if d[0] != 0x0B || d[1] != 0x77 {
        return None;
    }
    match d[5] >> 3 {
        0..=8 => {
            let size = AC3_FRAME_SIZE
                .get((d[4] & 0x3F) as usize)?
                .get((d[4] >> 6) as usize)?;
            Some((Ac3Stream::Ac3, *size))
        }
        11..=16 => Some((
            Ac3Stream::EAc3,
            (((d[2] & 0x07) as usize) << 8) + d[3] as usize + 1,
        )),
        _ => None,
    }
}

pub fn ac3_probe(tf: &mut TypeFind) -> Option<(u32, Ac3Stream)> {
    let mut scan = Scanner::new(tf, 0);
    while scan.offset() < AC3_WINDOW {
        let start = scan.offset();
        // A sync that leads nowhere must not drag the scan with it, so all of the
        // speculative reading happens on a copy of the cursor.
        let mut probe = scan;
        let Some(head) = probe.peek(6) else { break };
        let Some((stream, size)) = ac3_header(head) else {
            scan.consume(1);
            continue;
        };
        // Frame sizes are counted in 16-bit words, so twice the size is where
        // the next frame starts. A second sync there is what rules out a 0x0b77
        // sitting in other data by chance.
        probe.consume(2 * size);
        let again = match stream {
            // An AC-3 header spells out its own geometry, so the frame at the
            // far end has to agree that it is one too. E-AC-3 says no geometry
            // here, so the sync is all there is to compare. Both need a whole
            // header to be there, not just the two sync bytes.
            Ac3Stream::Ac3 => probe
                .peek(5)
                .is_some_and(|n| n[0] == 0x0B && n[1] == 0x77 && n[4] >> 6 < 3 && n[4] & 0x3F < 38),
            Ac3Stream::EAc3 => probe.peek(5).is_some_and(|n| n[0] == 0x0B && n[1] == 0x77),
        };
        if again {
            return Some((
                if start == 0 {
                    Prob::MAXIMUM.get()
                } else {
                    Prob::NEARLY_CERTAIN.get()
                },
                stream,
            ));
        }
        scan.consume(1);
    }
    None
}

// ---------------------------------------------------------------------------
// MP3
// ---------------------------------------------------------------------------

/// How far from where the probe starts to look for a first frame. Files with
/// junk in front exist, and so do files that are nothing but junk.
#[cfg(test)]
mod tests {
    use super::*;

    /// Hand-checked against the field layouts, so a shift that drifts from the
    /// table fails here and not only in a fixture sweep.
    #[test]
    fn header_fields_pack_as_the_tables_lay_them_out() {
        // frame_length 100 is 0b000001100100, split 2, 8 and 3 bits over octets
        // 3 to 5: nothing in octet 3, 01100 in octet 4, 100 on top of octet 5.
        // 13818-7 Table 8.
        assert_eq!(
            adts_frame_len(&[0xFF, 0xF1, 0, 0x00, 0x0C, 0x80]),
            Some(100)
        );
        // LC, 48 kHz, two channels: 00010 0011 0010, 14496-3 1.6.4.1.
        assert_eq!(adts_config(2, 3, 2), [0x11, 0x90]);
    }

    /// A 14-bit header repacked the way 102-114 means it. The two void bits of
    /// each word are discarded, so a word pulls its missing low bits from the
    /// fourteen data bits of the next and not from all sixteen.
    #[test]
    fn fourteen_bit_words_repack_to_the_sixteen_bit_ones() {
        let head = [
            0x1F, 0xFF, 0xE8, 0x00, 0x07, 0xF0, 0x3C, 0x7D, 0x2C, 0x7D, 0x30, 0x00, 0x24, 0xEE,
            0x00, 0x07,
        ];
        let w = DtsMarker::Be14.words(&head);
        assert_eq!(&w.unwrap()[2..6], &[0xFC3C, 0x7DB1, 0xF700, 0x093B]);
    }

    /// A zero audioMuxLengthBytes means the mux element carries no audio, so it
    /// is not a frame in either framing.
    /// Frame lengths recomputed by hand from 11172-3 and 13818-3 2.4.2.3, so
    /// that the code drifting from the tables fails here rather than in the
    /// field. Layer I counts four octets to a slot, and layer III at the lower
    /// rates has half the samples per frame.
    #[test]
    fn ac3_frame_sizes_follow_the_table() {
        let ac3 = |fscod: u8, frmsizecod: u8, bsid: u8| {
            ac3_header(&[0x0B, 0x77, 0, 0, (fscod << 6) | frmsizecod, bsid << 3])
        };
        for (fscod, frmsizecod, want) in [
            (0, 0, 64),
            (1, 0, 69),
            (2, 0, 96),
            (0, 11, 160),
            (1, 21, 418),
            (1, 27, 697),
            (2, 37, 1920),
        ] {
            assert_eq!(ac3(fscod, frmsizecod, 8), Some((Ac3Stream::Ac3, want)));
        }
        assert_eq!(ac3(0, 0, 9), None); // bsid 9 is neither
        assert_eq!(ac3(0, 0, 17), None); // past E-AC-3
        assert_eq!(ac3(3, 0, 0), None); // reserved sample rate
        assert_eq!(ac3(0, 38, 0), None); // past the table
        assert_eq!(ac3(0, 0, 16), Some((Ac3Stream::EAc3, 1)));
        // frmsiz spans the low three bits of d[2] and all of d[3].
        assert_eq!(
            ac3_header(&[0x0B, 0x77, 0x01, 0x2F, 0, 16 << 3]),
            Some((Ac3Stream::EAc3, 0x12F + 1))
        );
        assert_eq!(ac3_header(&[0x0A, 0x77, 0, 0, 0, 0]), None); // no sync
    }

    #[test]
    fn a_zero_payload_is_not_a_frame() {
        assert_eq!(loas_ep_frame_len(&[0x4D, 0xE1, 0, 0, 0]), None);
        assert_eq!(loas_frame_len(&[0x56, 0xE0, 0]), None);
    }
}
