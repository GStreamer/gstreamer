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
const MP3_WINDOW: i64 = 10_000;

/// Frames that claim the format, and the count past which looking further
/// cannot change the answer. Five agreeing headers have to fall inside
/// `MP3_WINDOW`, so a free format frame longer than a quarter of it cannot be
/// counted up, whatever its implied bitrate.
const MP3_MIN_HEADERS: usize = 2;
const MP3_TRY_HEADERS: usize = 5;

/// Frames one run may read, counted or not. A run that has read this many
/// without agreeing with itself is not a stream, and without the bound a run of
/// frames that disagree with each other reads to the end of the file for every
/// position the scan tries.
const MP3_MAX_FRAMES: usize = 16;

/// Confidence taken off a stream whose first frame was not the first thing the
/// probe came to.
const MP3_WRONG_HEADER: u32 = 10;

/// Confidence given back to a stream whose first frame is the first thing in
/// the file, which says plain MPEG audio rather than a stream that happens to
/// contain some.
const MP3_AT_ZERO: u32 = 10;

/// How far a free format frame may reach. The bitrate bound usually ends the
/// search well short of this, but not at the low sample rates, where the
/// implied bitrate only passes 1000 kbit/s past this point. There the read caps
/// the search and the upper half of the free format range goes unsearched.
const MP3_FREE_WINDOW: usize = 4608;

/// Bitrates in kbit/s by bitrate index: 11172-3 2.4.2.3 for MPEG-1 and
/// 13818-3 2.4.2.3 for MPEG-2, whose row MPEG-2.5 shares. Index 0 is free
/// format and index 15 is forbidden, so neither is ever read.
const MP3_BITRATES: [[[u32; 15]; 3]; 2] = [
    [
        [
            0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,
        ],
        [
            0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,
        ],
        [
            0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
        ],
    ],
    [
        [
            0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,
        ],
        [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
        [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
    ],
];

/// Sampling frequencies by index, 11172-3 2.4.2.3. The row is the version bits
/// less one, except that MPEG-2.5 takes the first: it is a de-facto extension
/// that neither part 3 describes.
const MP3_RATES: [[u32; 3]; 3] = [
    [11025, 12000, 8000],
    [22050, 24000, 16000],
    [44100, 48000, 32000],
];

/// MPEG audio layer, numbered 1 to 3 as the spec and the caps field number it.
/// The header carries it inverted: code 3 is layer 1, code 0 is no layer.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum MpegAudioLayer {
    One = 1,
    Two = 2,
    Three = 3,
}

impl MpegAudioLayer {
    fn from_code(code: u32) -> Option<MpegAudioLayer> {
        match code {
            3 => Some(MpegAudioLayer::One),
            2 => Some(MpegAudioLayer::Two),
            1 => Some(MpegAudioLayer::Three),
            _ => None,
        }
    }
}

/// What a frame header says about the frame it opens.
struct Mp3Frame {
    layer: MpegAudioLayer,
    /// Samples in a frame, which the format fixes per layer and version.
    samples: u32,
    /// Sample rate in hertz.
    sample_rate: u32,
    channels: u32,
    padding: u32,
    /// None when the bitrate index announces free format, in which case only
    /// the distance to the next header says how long the frame is.
    length: Option<usize>,
}

/// The frame header of 11172-3 2.4.1.3, one big-endian word. The version bits
/// pick the tables, and the value between MPEG-2 and MPEG-2.5 is no version at
/// all.
fn mp3_frame(word: u32) -> Option<Mp3Frame> {
    if word & 0xFFE0_0000 != 0xFFE0_0000 {
        return None;
    }
    let version = (word >> 19) & 3;
    let code = (word >> 17) & 3;
    let index = ((word >> 12) & 0xF) as usize;
    let rate_index = ((word >> 10) & 3) as usize;
    // A zero bitrate index is free format, whose length is in none of this.
    if version == 1 || index == 15 || rate_index == 3 {
        return None;
    }
    let layer = MpegAudioLayer::from_code(code)?;
    let mpeg1 = version == 3;
    // The row is the version less one, except that MPEG-2.5 takes the first.
    let row = if version == 0 {
        0
    } else {
        version as usize - 1
    };
    let sample_rate = MP3_RATES[row][rate_index];
    let padding = (word >> 9) & 1;
    // Layer I has 384 samples to a frame counted in slots of four octets.
    // Everything else has 1152, except that MPEG-2 layer III halves it, which
    // is what 13818-3 changes, 2.4.2.3.
    let samples = match (layer, mpeg1) {
        (MpegAudioLayer::One, _) => 384,
        (MpegAudioLayer::Three, false) => 576,
        _ => 1152,
    };
    // A frame of `samples` samples carrying k kbit/s reaches `samples * 125`
    // octets per kilohertz of sample rate. Layer I counts its padding in slots
    // rather than octets, so it keeps the form the spec gives it.
    let length = (index > 0).then(|| {
        let k = MP3_BITRATES[usize::from(!mpeg1)][layer as usize - 1][index];
        (if layer == MpegAudioLayer::One {
            ((12_000 * k / sample_rate) + padding) * 4
        } else {
            padding + samples * 125 * k / sample_rate
        }) as usize
    });
    Some(Mp3Frame {
        layer,
        samples,
        sample_rate,
        channels: 2 - u32::from((word >> 6) & 3 == 3),
        padding,
        length,
    })
}

/// Free format carries no bitrate, so the frame reaches to the next header that
/// agrees with this one on everything an encoder may not change. The implied
/// bitrate has to land inside what the format allows, which both rejects runs
/// of periodic data that merely look like frames and bounds the search without
/// a window. The bounds are the ones the parser that reads such a stream uses.
fn mp3_free_len(scan: &mut Scanner, word: u32, frame: &Mp3Frame) -> Option<usize> {
    // Bitrate, padding, mode and the trailing bits are all a later frame may
    // change. Everything else has to agree.
    const AGREEMENT: u32 = !((0xF << 12) | (1 << 9) | (0xF << 4) | 0xF);
    let want = word & AGREEMENT;
    let window = scan.peek(MP3_FREE_WINDOW)?;
    for len in 4..window.len().saturating_sub(3) {
        // The bitrate a frame this long would have to be carrying, in kbit/s.
        // Layer I counts four octets to a slot, so its padding is worth four.
        let slot = if frame.layer == MpegAudioLayer::One {
            4
        } else {
            1
        };
        let slots = len as u32 + slot - slot * frame.padding;
        let bitrate = frame.sample_rate * slots / (frame.samples * 125);
        if bitrate > 1_000 {
            return None;
        }
        let next = u32be(&window[len..]);
        if next & AGREEMENT != want || (next >> 12) & 0xF == 0xF {
            continue;
        }
        return (bitrate >= 8 && !(frame.layer == MpegAudioLayer::Three && bitrate > 640))
            .then_some(len);
    }
    None
}

/// Frames counted from the cursor on. A frame that disagrees with the one
/// before it about layer, rate or channel count is read and not counted,
/// because a run of real frames keeps to one of each. `short` says the run
/// ended because the stream did, which is what a fragment of a stream looks
/// like.
struct Mp3Run {
    found: usize,
    layer: Option<MpegAudioLayer>,
    changed: bool,
    short: bool,
    /// How far past the first header the run's evidence really starts. See
    /// `mp3_run`.
    lead: usize,
}

/// `None` when the octets at the cursor are no frame header at all, which is
/// what tells a missed sync from a stream that merely stops.
fn mp3_run(scan: &mut Scanner) -> Option<Mp3Run> {
    let mut run = Mp3Run {
        found: 0,
        layer: None,
        changed: false,
        short: false,
        lead: 0,
    };
    let mut prev: Option<Mp3Frame> = None;
    let mut seen = 0;
    while run.found < MP3_TRY_HEADERS && seen < MP3_MAX_FRAMES {
        let Some(head) = scan.peek(4) else {
            run.short = true;
            break;
        };
        let word = u32be(head);
        let Some(frame) = mp3_frame(word) else {
            // A run with no first frame at all is a missed sync, not a stream
            // that stopped.
            if prev.is_some() {
                break;
            }
            return None;
        };
        let Some(len) = frame.length.or_else(|| mp3_free_len(scan, word, &frame)) else {
            break;
        };
        // A frame the stream does not have in full is not a frame, and nothing
        // past it can be one either.
        if scan.peek(len).is_none() {
            run.short = true;
            break;
        }
        if prev.is_none() && frame.length.is_none() {
            // Nothing before this header said how long its frame is, so the
            // stream is only evidenced from the header that settles the length,
            // one frame on. Confidence is reckoned from there.
            run.lead = len;
        }
        if prev.as_ref().is_some_and(|p| {
            p.layer != frame.layer
                || p.sample_rate != frame.sample_rate
                || p.channels != frame.channels
        }) {
            run.changed = true;
        } else {
            run.found += 1;
        }
        // The layer reported is the last header the run read, whether or not
        // that header was one of the agreeing ones.
        run.layer = Some(frame.layer);
        seen += 1;
        scan.consume(len);
        prev = Some(frame);
    }
    Some(run)
}

/// The frames at `start` and what to make of them. Five frames settle it. Two
/// that the stream cuts short settle it too, which is what a file holding only
/// the tail of a stream looks like.
fn mp3_scan(tf: &mut TypeFind, start: i64) -> (u32, Option<MpegAudioLayer>) {
    let mut scan = Scanner::new(tf, start);
    let mut missed = false;
    while scan.offset() < start + MP3_WINDOW {
        let at = scan.offset();
        let Some(head) = scan.peek(4) else { break };
        if head[0] != 0xFF {
            scan.consume(1);
            continue;
        }
        // The run reads ahead as far as the frames reach, which says nothing
        // about where the next candidate starts. A copy of the cursor reads
        // ahead; the scan stays on the byte it is trying.
        let mut probe = scan;
        let Some(run) = mp3_run(&mut probe) else {
            missed = true;
            scan.consume(1);
            continue;
        };
        let accepted = run.found == MP3_TRY_HEADERS || (run.found >= MP3_MIN_HEADERS && run.short);
        if !accepted {
            scan.consume(1);
            continue;
        }
        let skipped = (at - start) as u32 + run.lead as u32;
        // `lead` is a frame length, so `skipped` can pass the window and
        // underflow it. Past the window there is nothing left to count.
        let inside = (MP3_WINDOW as u32).saturating_sub(skipped);
        let mut probability = run.found as u32 * Prob::MAXIMUM.get() * inside
            / MP3_TRY_HEADERS as u32
            / MP3_WINDOW as u32;
        if missed && probability > Prob::MINIMUM.get() + MP3_WRONG_HEADER {
            probability -= MP3_WRONG_HEADER;
        }
        probability = probability.max(Prob::MINIMUM.get());
        if start > 0 {
            probability /= 2;
        }
        if !run.changed {
            probability = (probability + Prob::MAXIMUM.get()) / 2;
        }
        return (probability, run.layer);
    }
    (0, None)
}

/// What the MP3 probe decided.
pub struct Mp3Stream {
    pub probability: u32,
    pub layer: i32,
}

/// Free format, bitrate index zero, is read by the same rule the parser that
/// consumes such a stream uses, so the typefinder finds what that parser can
/// then parse.
pub fn mp3_probe(tf: &mut TypeFind) -> Option<Mp3Stream> {
    let found = 'merge: {
        let (mut probability, layer) = mp3_scan(tf, 0);
        let stream = |probability: u32, layer: Option<MpegAudioLayer>| {
            (probability > 0).then_some(Mp3Stream {
                probability,
                layer: layer.map_or(0, |layer| layer as i32),
            })
        };
        // Nothing at the start says nothing: a stream can begin anywhere, so
        // look halfway in and see whether the middle agrees with the start.
        let Some(length) = tf.length() else {
            break 'merge stream(probability, layer);
        };
        if probability >= Prob::LIKELY.get() {
            break 'merge stream(probability, layer);
        }
        let (mid, mid_layer) = mp3_scan(tf, length.get() as i64 / 2);
        if mid > 0 {
            if probability == 0 {
                break 'merge stream(mid, mid_layer);
            }
            // Two runs that disagree about the layer are two different things.
            if layer != mid_layer {
                break 'merge None;
            }
            probability = (probability + mid) / 2;
            break 'merge stream(probability, layer);
        }
        // A header right at the start says plain MPEG audio rather than a
        // stream that merely happens to contain some.
        if probability > 0
            && let Some(head) = tf.peek::<4>(0)
            && mp3_frame(u32be(head)).is_some_and(|f| f.length.is_some())
        {
            probability = (probability + MP3_AT_ZERO).min(Prob::MAXIMUM.get());
        }
        stream(probability, layer)
    };
    // An ID3v1 tag describes the audio rather than being it, so a file that
    // ends with one is left to whatever reads the tag. Only asked once there is
    // something to overrule, since the answer costs a read near the end.
    found.filter(|_| tf.peek_var(-128, 3).is_none_or(|tag| tag != b"TAG"))
}

// ---------------------------------------------------------------------------
// SBC
// ---------------------------------------------------------------------------

/// SBC frames sit in no container and carry no tag. The check is therefore a
/// chain of frames: every header states its own length, so the next sync octet
/// has to sit exactly where the previous header points. A2DP Appendix B,
/// Tables 12.12 and 12.16 to 12.20.
const SBC_SYNC: u8 = 0x9C;
/// Sampling frequency by the two-bit code, 12.5.1 Table 12.16.
const SBC_RATES: [u32; 4] = [16000, 32000, 44100, 48000];
/// Frames that settle it.
const SBC_FRAMES: usize = 4;

/// What a frame header says about the stream it opens.
#[derive(Clone, Copy)]
pub struct SbcFrame {
    len: usize,
    pub rate: u32,
    pub channels: usize,
}

/// The frame the octets at the cursor open, or `None` when they are no header.
///
/// The bitpool bound comes from 12.5.1: sixteen octets per subband for mono and
/// dual channel, thirty two for the two stereo modes.
///
/// The length follows 12.9 and rounds the bitstream up to a whole octet. The
/// join field is present in full whenever the mode is joint stereo and absent
/// otherwise, so it adds its bits to every joint frame and to no other,
/// whatever marks it carries.
///
/// The header CRC is not checked. It covers the scale factors as well as the
/// header, so checking it means reading the frame body. A chain of frames that
/// agree on their spacing is not something that payload reaches by chance.
fn sbc_frame(head: &[u8]) -> Option<SbcFrame> {
    let [sync, config, bitpool, _crc]: [u8; 4] = head.get(..4)?.try_into().ok()?;
    if sync != SBC_SYNC {
        return None;
    }
    let bitpool = usize::from(bitpool);
    let mode = (config >> 2) & 3;
    let stereo = mode >= 2;
    let subbands = 4usize << (config & 1);
    let channels = if mode == 0 { 1 } else { 2 };
    let blocks = 4 * (((config >> 4) & 3) as usize + 1);
    let limit = if stereo { 32 * subbands } else { 16 * subbands };
    if bitpool < 2 || bitpool > limit {
        return None;
    }
    let join = usize::from(mode == 3) * subbands;
    let scale = 4 * subbands * channels / 8;
    let bits = if stereo {
        join + blocks * bitpool
    } else {
        blocks * channels * bitpool
    };
    Some(SbcFrame {
        len: 4 + scale + bits.div_ceil(8),
        rate: SBC_RATES[(config >> 6) as usize],
        channels,
    })
}

/// Whether a chain of frames starts at the cursor, and what the first header
/// says about the stream. Each frame has to be present in full, because a frame
/// the stream does not have is not a frame.
fn sbc_chain(scan: &mut Scanner) -> Option<SbcFrame> {
    let first = sbc_frame(scan.peek(4)?)?;
    scan.peek(first.len)?;
    scan.consume(first.len);
    for _ in 1..SBC_FRAMES {
        let frame = sbc_frame(scan.peek(4)?)?;
        scan.peek(frame.len)?;
        scan.consume(frame.len);
    }
    Some(first)
}

/// Whether the stream is SBC, and what the first frame says about it. The
/// first frame starts at the first octet: a bare SBC stream carries nothing
/// ahead of the audio. The caps give the sample rate and the channel count,
/// and a decoder reads the rest from the frames.
pub fn sbc_probe(tf: &mut TypeFind) -> Option<SbcFrame> {
    sbc_chain(&mut Scanner::new(tf, 0))
}

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
    fn frame_lengths_follow_the_bitrate_tables() {
        for (word, want) in [
            // MPEG-1 L3 128k 44.1
            (0xFFFB9000, Some(417)),
            // MPEG-1 L1 448k 48
            (0xFFFFE400, Some(448)),
            // MPEG-1 L2 384k 48, padded
            (0xFFFDE600, Some(1153)),
            // MPEG-2 L3 8k 24
            (0xFFF31400, Some(24)),
            // MPEG-2 L2 48k 22.05
            (0xFFF53000, Some(156)),
            // MPEG-2.5 L3 8k 12
            (0xFFE31400, Some(48)),
            // MPEG-1 L3 320k 32, padded
            (0xFFFBEA00, Some(1441)),
        ] {
            assert_eq!(mp3_frame(word).map(|f| f.length), want.map(Some));
        }
        // Free format carries no length, and the three forbidden header values
        // are no frame at all.
        assert_eq!(mp3_frame(0xFFFB0000).unwrap().length, None);
        assert!(mp3_frame(0xFFFBF000).is_none()); // bitrate index 15
        assert!(mp3_frame(0xFFFB0C00).is_none()); // sampling index 3
        assert!(mp3_frame(0xFFEB9000).is_none()); // version 1
    }

    /// Frame sizes straight out of A/52 table 5.18, which counts 16-bit words,
    /// and the E-AC-3 rule that `frmsiz` is the word count less one.
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

    /// The frame length is what the chain check trusts, so the expectation
    /// comes from A2DP Table 4.7, which lists frame lengths for known
    /// settings, and not from this code. Every case is 16 blocks, loudness
    /// allocation and 8 subbands; the config octet packs the sampling frequency
    /// and the mode on top of that.
    #[test]
    fn sbc_frame_length_matches_the_reference_table() {
        let cases = [
            (0xB1, 19, 46),  // 44.1 kHz mono, bitpool 19
            (0xF1, 18, 44),  // 48 kHz mono, bitpool 18
            (0xB1, 31, 70),  // 44.1 kHz mono, bitpool 31
            (0xF1, 29, 66),  // 48 kHz mono, bitpool 29
            (0xBD, 35, 83),  // 44.1 kHz joint stereo, bitpool 35
            (0xFD, 33, 79),  // 48 kHz joint stereo, bitpool 33
            (0xBD, 53, 119), // 44.1 kHz joint stereo, bitpool 53
            (0xFD, 51, 115), // 48 kHz joint stereo, bitpool 51
        ];
        for (config, bitpool, want) in cases {
            let head = [SBC_SYNC, config, bitpool, 0x00];
            assert_eq!(sbc_frame(&head).map(|f| f.len), Some(want));
        }
    }

    /// The join field is sized by the mode, not by the marks inside it, so the
    /// marks cannot move the frame boundary. Counting the marks instead reads
    /// 12.9 wrongly and puts every sync octet of a joint stream out of reach.
    #[test]
    fn the_join_field_does_not_depend_on_its_marks() {
        let len = |marks| sbc_frame(&[SBC_SYNC, 0xFD, 33, 0x00, marks]).map(|f| f.len);
        assert_eq!(len(0x00), Some(79));
        assert_eq!(len(0x00), len(0xFE));
    }

    /// A header that does not open with the sync octet is not a header, and the
    /// chain check depends on that for every frame in it, not just the first.
    #[test]
    fn octets_without_the_sync_do_not_open_a_frame() {
        assert_eq!(sbc_frame(&[0x00, 0xF1, 18, 0x00]).map(|f| f.len), None);
        assert_eq!(
            sbc_frame(&[SBC_SYNC, 0xF1, 18, 0x00]).map(|f| f.len),
            Some(44)
        );
    }
}
