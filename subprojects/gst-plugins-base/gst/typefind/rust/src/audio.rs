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
    Prob, Scanner, TypeFind, aac_channels, aac_level, aac_profile, aac_sample_rate, u16be,
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
    fn a_zero_payload_is_not_a_frame() {
        assert_eq!(loas_ep_frame_len(&[0x4D, 0xE1, 0, 0, 0]), None);
        assert_eq!(loas_frame_len(&[0x56, 0xE0, 0]), None);
    }
}
