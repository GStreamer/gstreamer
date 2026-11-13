// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Probes for the MPEG-2 systems family, the MPEG-1/2 video elementary stream
//! and the MPEG-4 visual elementary stream.
//!
//! All of them are built on the same start code prefix, `00 00 01`, followed by
//! an octet that names what it starts. Which names belong to which format is
//! what separates them: `itu-t-h.222.0-2023-08` Table 2-22 assigns the stream
//! ids from `0xBC` upward to PES streams, `0xBA` and `0xBB` are the pack and
//! system headers of a program stream, and `itu-t-h.262-2000-02` Table 6-1
//! keeps everything below `0xB9` for the MPEG-1/2 video stream. The names
//! `iso-14496-2` Table 6-3 gives MPEG-4 visual open below `0xB0` and run on to
//! `C3`, so they overlap both the MPEG-1/2 video range and the range Table 2-22
//! gives to PES streams. The two video formats are told apart by the headers
//! they put after the prefix, not by the names themselves.
//!
//! A program stream and a video elementary stream therefore claim overlapping
//! octets, and the pack header is the tie-breaker: a valid one means the octets
//! belong to a multiplexed program, so both elementary finders decline and the
//! program stream finder is left to claim them. That is why all three share
//! [`valid_pack`].

use crate::helpers::{Prob, Scanner, TypeFind, u16be};
use crate::startcode::{START_CODE_PREFIX_LEN, is_start_code, next_start_code};

/// Start code value of a pack header, H.222.0 2.5.3.4.
const PACK_CODE: u8 = 0xBA;
/// Start code value of a system header, H.222.0 2.5.3.6.
const SYSTEM_CODE: u8 = 0xBB;
/// Table 2-22 assigns every stream id from `0xBC` upward to a stream. `0xBA`
/// and `0xBB` name the pack and system headers and are matched before this.
const PES_CODE_MIN: u8 = 0xBC;

/// Longest program stream the systems probe examines.
const SYS_MAX_PROBE: usize = 128 * 1024;
/// Shortest program stream it will examine at all.
const SYS_MIN_PROBE: usize = 16;
/// A run of headers has to exceed this count to be worth suggesting.
const SYS_MIN_HEADERS: u32 = 2;
/// The four octets `00 00 01 00`: what the rolling word reads when the three
/// before the cursor are the start code prefix.
const PREFIX_SYNC: u32 = 0x0000_0100;
/// The walk stops once a run that includes a PES packet passes this count.
/// Nothing further in the stream can change the answer, so reading on would be
/// work for nothing.
const SYS_MAX_HEADERS: u32 = 5;

/// Transport packet sizes a stream may carry: the standard 188, the DVHS 192,
/// and 188 with 16 or 20 octets of Reed-Solomon.
const TS_PACKET_SIZES: [i32; 4] = [188, 192, 204, 208];
const TS_MAX_PACKET_SIZE: usize = 208;
/// How many headers at one stride make a transport stream, and the most the
/// stride walk counts before it is satisfied.
const TS_MIN_HEADERS: u32 = 4;
const TS_MAX_HEADERS: u32 = 10;
const TS_SCAN_LENGTH: i64 = TS_MAX_HEADERS as i64 * TS_MAX_PACKET_SIZE as i64 * 4;
/// Octets a packet header occupies: the sync byte, the two carrying the flags
/// and PID, and the one carrying `adaptation_field_control`.
///
/// The size a header occupies is also the fewest octets a probe must have in
/// view to read one, so it is the size the walk asks the engine for.
const TS_HEADER_SIZE: usize = 4;

/// How far the video probe looks, and how many pictures it wants to pair up.
const VID_TRY_SYNC: i64 = 100 * 1024;
const VID_TRY_PICTURES: u32 = 6;
/// Sequence, group, picture and slice headers, Table 6-1.
const SEQUENCE_CODE: u8 = 0xB3;
const GROUP_CODE: u8 = 0xB8;
const PICTURE_CODE: u8 = 0x00;
const FIRST_SLICE_CODE: u8 = 0x01;
/// A first slice header belongs to the picture header before it when it sits
/// no more than 64 octets after it. The shortest gap the walk can ever see is
/// the 8 octets a picture header steps over.
const SLICE_MAX_GAP: i64 = 64;
/// Longest pack header, so that the header after it can be inspected: 14
/// octets, at most 7 of stuffing, and the 4 that make up the next start code.
const PACK_MAX_SCAN: usize = 14 + 7 + 4;

/// Whether the pack header at the start of `data` is one the format can
/// produce, and its length including the start code and any stuffing.
///
/// Table 2-39 of H.222.0 lays the MPEG-2 pack header out as `pack_start_code`,
/// then `'01'`, the 33-bit `system_clock_reference_base` in runs of 3, 15 and
/// 15 bits each closed by a `marker_bit`, the 9-bit
/// `system_clock_reference_extension` and its `marker_bit`, the 22-bit
/// `program_mux_rate`, two `marker_bit`s, 5 reserved bits and the 3-bit
/// `pack_stuffing_length`. Laid out that way, the fixed bits land on octet 4
/// (`'01'` and the marker), octet 6 bit 2, octet 8 bit 2, octet 9 bit 0, octet
/// 12 bits 1 and 0, and the stuffing length in octet 13 bits 2 to 0.
///
/// The MPEG-1 form is 12 octets long and opens its fifth octet with `'0010'`.
///
/// A pack header that has octets after it has to be followed by another start
/// code, which is what stops a `00 00 01 BA` that occurs by chance inside a
/// compressed frame from being read as one.
///
/// The answer carries which version the header is, because telling the two
/// forms apart is what the mask on the fifth octet already does.
fn valid_pack(data: &[u8]) -> Option<(usize, i32)> {
    if data.len() < 12 {
        return None;
    }
    if data[4] & 0xC4 == 0x44 {
        if data.len() < 14 {
            return None;
        }
        if data[6] & 0x04 != 0x04
            || data[8] & 0x04 != 0x04
            || data[9] & 0x01 != 0x01
            || data[12] & 0x03 != 0x03
        {
            return None;
        }
        let stuff = (data[13] & 0x07) as usize;
        // The header after the pack is only inspected when the stream reaches it.
        if data.len() >= 14 + stuff + 4 && !is_start_code(&data[14 + stuff..]) {
            return None;
        }
        Some((14 + stuff, 2))
    } else if data[4] & 0xF1 == 0x21 {
        if data[6] & 0x01 != 0x01
            || data[8] & 0x01 != 0x01
            || data[9] & 0x80 != 0x80
            || data[11] & 0x01 != 0x01
        {
            return None;
        }
        if data.len() >= 12 + 4 && !is_start_code(&data[12..]) {
            return None;
        }
        Some((12, 1))
    } else {
        None
    }
}

/// Whether the PES packet header at the start of `data` says how long the
/// packet is, and that length including the start code.
///
/// `PES_packet_length` is the number of octets after that field. A program
/// stream may not carry an unbounded PES packet, so a zero length names nothing
/// to step over.
fn valid_pes(data: &[u8]) -> Option<usize> {
    if data.len() < 6 {
        return None;
    }
    let length = u16be(&data[4..]) as usize;
    if length == 0 {
        return None;
    }
    if data.len() >= 6 + length + 4 && !is_start_code(&data[6 + length..]) {
        return None;
    }
    Some(6 + length)
}

/// Whether the system header at the start of `data` says how long it is, and
/// that length including the start code. Table 2-40 gives `header_length` as
/// the octets after that field, and the fields it introduces are 6 octets
/// before the stream loop begins, so a shorter header is not one.
fn valid_system_header(data: &[u8]) -> Option<usize> {
    if data.len() < 6 {
        return None;
    }
    let length = u16be(&data[4..]) as usize;
    if length < 6 {
        return None;
    }
    if data.len() >= 6 + length + 4 && !is_start_code(&data[6 + length..]) {
        return None;
    }
    Some(6 + length)
}

/// Walk the program stream in `data` and settle the confidence in it, as a
/// probability and an MPEG version.
///
/// Only headers that sit close to the previous one count. The gap is measured
/// from the end of the packet before it, and a match already stands three
/// octets into its own start code, so two octets of anything between two
/// packets is enough to restart the count. A stream that merely happens to
/// contain a few `00 00 01` sequences therefore never adds up. A header whose
/// length is known is stepped over whole, which is what makes a run of packs,
/// system headers and PES packets read as one run.
fn sys_scan(data: &[u8]) -> Option<(u32, i32)> {
    let end = data.len();
    let mut mpegversion = 0i32;
    let mut pack_headers = 0u32;
    let mut pes_headers = 0u32;
    let mut potential_headers = 0u32;
    // Whether the first start code the walk matched began on the first octet.
    // That is the only question ever asked of where the run began.
    let mut first_at_zero = false;
    // A match needs three prefix octets behind the octet that carries the
    // code, so wherever the walk first becomes able to match it has already
    // crossed three octets since the last sync. The count decides whether
    // packets are treated as one run or as separate ones, so the three octets
    // a match stands behind have to be in it.
    let mut since_last_sync = 3u32;
    // Index of the octet carrying a start code value; the three before it have
    // to be the prefix, so the walk starts where that first becomes possible.
    let mut i = 3usize;
    // The four octets ending at `i - 1`, as a value. Shifting it left by one
    // octet and comparing against the prefix leaves one compare per octet
    // instead of a three-octet test at every position. A stream too short to
    // hold a prefix never enters the walk, so the word is never read.
    let mut sync = data
        .get(..3)
        .map_or(0, |head| u32::from_be_bytes([0, head[0], head[1], head[2]]));

    // Once enough headers have been seen, and some of them were PES packets,
    // the stream is settled. Nothing further in it should be able to change the
    // answer, so the walk stops rather than reading on.
    while i < end && !(pes_headers > 0 && pack_headers + pes_headers > SYS_MAX_HEADERS) {
        sync <<= 8;
        if sync == PREFIX_SYNC {
            if potential_headers == 0 {
                // A match stands three octets behind the octet carrying its
                // value, so one on the first octet leaves the cursor here.
                first_at_zero = i == START_CODE_PREFIX_LEN as usize;
            }
            if since_last_sync > 4 {
                pes_headers = 0;
                pack_headers = 0;
            }
            potential_headers += 1;

            let code = data[i];
            let rest = &data[i - 3..end];
            let packet = if code == PACK_CODE {
                valid_pack(rest).map(|(size, version)| {
                    // The version comes out of a header already known to be a
                    // pack, not out of an octet that only looks like the start
                    // of one.
                    mpegversion = version;
                    pack_headers += 1;
                    size
                })
            } else if code >= PES_CODE_MIN {
                valid_pes(rest).inspect(|_| {
                    pes_headers += 1;
                    if mpegversion == 0 {
                        mpegversion = 2;
                    }
                })
            } else if code == SYSTEM_CODE {
                valid_system_header(rest).inspect(|_| pack_headers += 1)
            } else {
                None
            };

            if let Some(size) = packet {
                // The next candidate start code begins right after the packet,
                // and a match stands three octets behind the octet carrying its
                // value, so the cursor goes to where that value would be.
                i += size;
                since_last_sync = 3;
                if i < end {
                    sync = u32::from_be_bytes([0, data[i - 3], data[i - 2], data[i - 1]]);
                }
                continue;
            }
        }

        sync |= u32::from(data[i]);
        since_last_sync += 1;
        i += 1;
    }

    if pes_headers == 0 || pack_headers + pes_headers <= SYS_MIN_HEADERS {
        return None;
    }

    let maximum = Prob::MAXIMUM.get();
    let mut probability = Prob::POSSIBLE.get() + 10 * (pack_headers + pes_headers);
    probability = probability.min(maximum);
    // More confidence when every start code seen was one this format defines,
    // which is the case for a stream read from only a handful of octets.
    if potential_headers == pack_headers + pes_headers {
        probability = (probability + 10).min(maximum);
    }
    // Less when the first packet was not right at the start of the stream. The
    // run that got here is worth at least three headers, so the floor of the
    // ladder is 50 + 30 and the subtraction can never underflow.
    if !first_at_zero {
        probability -= 10;
    }
    Some((probability, mpegversion))
}

/// MPEG program stream: packs, a system header and PES packets following one
/// another.
pub fn mpeg_sys_probe(tf: &mut TypeFind) -> Option<(Prob, i32)> {
    let mut len = SYS_MAX_PROBE;
    while len >= SYS_MIN_PROBE && tf.peek_var(0, len as u32).is_none() {
        len /= 2;
    }
    if len < SYS_MIN_PROBE {
        return None;
    }
    let data = tf.peek_var(0, len as u32)?;
    let (probability, mpegversion) = sys_scan(data)?;
    Some((probability.into(), mpegversion))
}

/// Whether the four octets at the start of `data` open a transport packet.
/// `sync_byte` is a fixed `0x47` and `transport_error_indicator` is the top bit
/// of the second octet. `adaptation_field_control` is bits 5 and 4 of the
/// fourth, and Table 2-5 reserves `'00'`.
// shortcut: `'00'` is still accepted on a null packet, PID `0x1FFF`. 2.4.3.3
// says such a packet shall be discarded and that a null packet's field shall be
// `'01'`, so this is leniency for muxers that get it wrong, not a case the spec
// allows. Drop the second clause for a strict reading.
fn ts_header(data: &[u8]) -> bool {
    data[0] == 0x47
        && data[1] & 0x80 == 0
        && (data[3] & 0x30 != 0 || (data[1] & 0x1F == 0x1F && data[2] == 0xFF))
}

/// MPEG transport stream: `0x47` at a constant stride. A capture can begin in
/// the middle of a packet, so the stride is searched for rather than assumed,
/// and each of the four packet sizes is tried at every candidate offset.
pub fn mpeg_ts_probe(tf: &mut TypeFind) -> Option<(Prob, i32)> {
    let mut scan = Scanner::new(tf, 0);

    while scan.offset() < TS_SCAN_LENGTH {
        let Some(head) = scan.peek(TS_HEADER_SIZE) else {
            break;
        };
        if ts_header(head) {
            for size in TS_PACKET_SIZES {
                // How many packet headers lie at this stride, counting the one
                // already found. The walk stops at the first offset that does
                // not carry a header.
                let mut stride = scan;
                let mut found = 1u32;
                while found < TS_MAX_HEADERS {
                    stride.consume(size as usize);
                    let Some(next) = stride.peek(TS_HEADER_SIZE) else {
                        break;
                    };
                    if !ts_header(next) {
                        break;
                    }
                    found += 1;
                }
                if found >= TS_MIN_HEADERS {
                    // One tenth per header found, so four is the floor and ten
                    // the ceiling.
                    let probability = (10 * found).min(Prob::MAXIMUM.get());
                    return Some((probability.into(), size));
                }
            }
        }

        scan.consume(1);
    }
    None
}

/// The confidence in an MPEG-1/2 video elementary stream, from what the walk
/// saw. `paired` counts picture headers that a first slice header followed;
/// the rungs below it are the shapes a partial or damaged stream leaves.
fn video_probability(
    paired: u32,
    seen_seq_at_0: bool,
    seen_seq: bool,
    seen_gop: bool,
) -> Option<Prob> {
    if paired == 0 && !seen_seq {
        return None;
    }
    let full = paired >= VID_TRY_PICTURES;
    let several = paired > 2;
    let any = paired > 0;

    let probability = if full && seen_seq && seen_gop {
        Prob::NEARLY_CERTAIN.get() - 1
    } else if full && seen_seq {
        Prob::NEARLY_CERTAIN.get() - 9
    } else if full {
        Prob::LIKELY.get()
    } else if seen_seq_at_0 && seen_gop && several {
        Prob::LIKELY.get() - 10
    } else if seen_seq && seen_gop && several {
        Prob::LIKELY.get() - 20
    } else if seen_seq_at_0 && any {
        Prob::POSSIBLE.get()
    } else if seen_seq && any {
        Prob::POSSIBLE.get() - 5
    } else if any {
        Prob::POSSIBLE.get() - 10
    } else {
        Prob::POSSIBLE.get() - 20
    };
    Some(probability.into())
}

/// MPEG-1 and MPEG-2 video elementary stream: a sequence header, optionally a
/// group header, then picture headers each followed by the slice header that
/// opens its first macroblock row.
pub fn mpeg_video_probe(tf: &mut TypeFind) -> Option<Prob> {
    // How much of a pack header can be inspected at the last positions of the
    // stream decides whether the header after it is reached. When the stream
    // states its length the bound is what it has left, so a pack near the end
    // is judged on the same octets however they were read.
    let stream_len = tf.length().map(|len| len.get());
    let mut scan = Scanner::new(tf, 0);
    let mut seen_seq_at_0 = false;
    let mut seen_seq = false;
    let mut seen_gop = false;
    let mut picture_headers = 0u32;
    let mut paired = 0u32;
    let mut last_pic_offset = 0i64;

    while scan.offset() < VID_TRY_SYNC && paired < VID_TRY_PICTURES {
        let Some(head) = scan.peek(5) else { break };
        if !is_start_code(head) {
            scan.consume(1);
            continue;
        }
        let code = head[3];

        if code == PACK_CODE {
            let want = stream_len.map_or(PACK_MAX_SCAN, |len| {
                PACK_MAX_SCAN.min(len.saturating_sub(scan.offset() as u64) as usize)
            });
            if scan
                .peek(want)
                .is_some_and(|pack| valid_pack(pack).is_some())
            {
                // A pack header means a multiplexed program stream, so this is
                // not an elementary video stream and nothing is suggested.
                return None;
            }
        }

        if code == SEQUENCE_CODE {
            seen_seq_at_0 |= scan.offset() == 0;
            seen_seq = true;
            scan.consume(4 + 8);
        } else if code == GROUP_CODE {
            seen_gop = true;
            scan.consume(8);
        } else if code == PICTURE_CODE {
            picture_headers += 1;
            last_pic_offset = scan.offset();
            scan.consume(8);
        } else if code == FIRST_SLICE_CODE
            // One slice header per picture header, and the first slice of a
            // picture starts with slice_vertical_position 1 (6.3.16).
            && picture_headers > paired
            && (scan.offset() - last_pic_offset) <= SLICE_MAX_GAP
        {
            scan.consume(4);
            paired += 1;
        } else {
            scan.consume(1);
        }
    }

    video_probability(paired, seen_seq_at_0, seen_seq, seen_gop)
}

/// Start code values of the MPEG-4 visual stream, from `iso-14496-2`
/// Table 6-3. `00` to `1F` is one `video_object_start_code` per video object
/// and `20` to `2F` one `video_object_layer_start_code` per layer, so those two
/// are ranges rather than names.
const M4V_VIDEO_OBJ_CODE_MAX: u8 = 0x1F;
const M4V_VIDEO_LAYER_CODE_MIN: u8 = 0x20;
const M4V_VIDEO_LAYER_CODE_MAX: u8 = 0x2F;
const M4V_VIOS_CODE: u8 = 0xB0;
/// `user_data_start_code`, which a visual object sequence may carry between its
/// own header and the object that follows it.
const M4V_USER_DATA_CODE: u8 = 0xB2;
const M4V_VISUAL_OBJ_CODE: u8 = 0xB5;
const M4V_VOP_CODE: u8 = 0xB6;

/// How far the MPEG-4 visual probe looks, and how many pictures it wants to
/// count before it is satisfied.
const M4V_TRY_SYNC: i64 = 100 * 1024;
const M4V_TRY_PICTURES: u32 = 6;

// shortcut: headers are stepped over by a fixed count rather than measured. The
// count is what the format puts between the code octet and the next start code:
// one octet of profile_and_level_indication after a sequence header, one octet
// of visual_object_type after a visual object, and nothing at all after a video
// object, whose id is the five bits of the code octet itself. The layer and
// picture steps land inside the header they name, which costs nothing because
// the search after them spans the rest of the probe.
const M4V_VIOS_STEP: usize = 2;
const M4V_VISUAL_OBJ_STEP: usize = 2;
const M4V_VIDEO_OBJ_STEP: usize = 1;
const M4V_VIDEO_LAYER_STEP: usize = 5;
const M4V_VOP_STEP: usize = 2;
/// How far past a visual object header the next start code is looked for. The
/// mandatory fields, then `video_signal_type` with `colour_description` and its
/// colour tables, put a conforming next start code about six octets out; 7 is
/// that with room, not a distance the format states.
const M4V_VISUAL_OBJ_SLACK: usize = 7;
/// How far past a `user_data_start_code` the next start code is looked for.
/// User data is an opaque run with no length field, so it is crossed by
/// searching rather than by measuring.
// shortcut: 24 is a chosen cutoff, not a distance the format states. User data
// has no length of its own, so a run longer than this hides the next start code
// and the walk ends where that search fails.
const M4V_USER_DATA_SLACK: usize = 24;

/// The confidence in an MPEG-4 visual elementary stream, from what the walk
/// saw. `vops` counts picture headers; the rungs below the top ones are the
/// shapes a stream that has lost its sequence, object or layer headers leaves.
///
/// A stream that showed a layer header and nothing else matches no rung and is
/// not suggested at all. A suggestion carrying no confidence would say no more
/// than silence does.
fn mpeg4_video_probability(
    vops: u32,
    seen_vios_at_0: bool,
    seen_vios: bool,
    seen_vos: bool,
    seen_vol: bool,
) -> Option<Prob> {
    if vops == 0 && !seen_vol {
        return None;
    }
    let full = vops >= M4V_TRY_PICTURES;
    let several = vops > 2;
    let any = vops > 0;

    let probability = if full && seen_vios_at_0 && seen_vos && seen_vol {
        Prob::MAXIMUM.get() - 1
    } else if full && seen_vios && seen_vos && seen_vol {
        Prob::NEARLY_CERTAIN.get() - 1
    } else if (seen_vios_at_0 || full) && seen_vos && seen_vol {
        // Either a sequence header on the very first octet or a full run of
        // pictures reaches this rung with the video object and its layer, and
        // neither one needs the other.
        Prob::NEARLY_CERTAIN.get() - 6
    } else if full && seen_vol {
        Prob::NEARLY_CERTAIN.get() - 9
    } else if full {
        Prob::LIKELY.get() - 1
    } else if several && seen_vios && seen_vos && seen_vol {
        Prob::LIKELY.get() - 9
    } else if seen_vios && seen_vos && seen_vol {
        Prob::LIKELY.get() - 20
    } else if any && seen_vos && seen_vol {
        Prob::POSSIBLE.get()
    } else if any {
        Prob::POSSIBLE.get() - 10
    } else if seen_vos && seen_vol {
        Prob::POSSIBLE.get() - 20
    } else {
        return None;
    };
    Some(probability.into())
}

/// MPEG-4 visual elementary stream: a visual object sequence header, a visual
/// object header, then video object, video object layer and picture headers.
/// The walk counts at most one sequence header and one video object and stops
/// at a second of either, so a stream that repeats them is judged on what came
/// before the repeat.
///
/// A sequence header and a visual object header each read on to the start code
/// after them and take its value, so one pass can settle a video object. A
/// sequence header may cross a user data block on the way, and whatever that
/// search lands on is read the same way as any other code.
pub fn mpeg4_video_probe(tf: &mut TypeFind) -> Option<Prob> {
    // How much of a pack header can be inspected at the last positions of the
    // stream decides whether the header after it is reached. When the stream
    // states its length the bound is what it has left, so a pack near the end
    // is judged on the same octets however they were read.
    let stream_len = tf.length().map(|len| len.get());
    let mut scan = Scanner::new(tf, 0);
    let mut seen_vios_at_0 = false;
    let mut seen_vios = false;
    let mut seen_vos = false;
    let mut seen_vol = false;
    let mut vops = 0u32;

    while scan.offset() < M4V_TRY_SYNC && vops < M4V_TRY_PICTURES {
        // A search tries one position more than the room it is given, so the
        // room is one less than what the probe has left.
        let room = (M4V_TRY_SYNC - scan.offset() - 1) as usize;
        if next_start_code(&mut scan, room).is_none() {
            break;
        }
        // A search only answers once four octets were in view and it stepped
        // three of them, so the octet it leaves the cursor on is always there.
        let Some(head) = scan.peek(1) else { break };
        let mut code = head[0];

        if code == PACK_CODE {
            // A pack header means this video arrives inside a multiplexed
            // program stream, and the program stream finder is the one that
            // should claim it. The header begins three octets behind the
            // code. The value doubles as this format's
            // fba_object_start_code (14496-2): an FBA extension whose
            // octets parse as a pack is declined here, where the old
            // typefinder would read on and claim the stream.
            let start = scan.offset() - START_CODE_PREFIX_LEN;
            let want = stream_len.map_or(PACK_MAX_SCAN, |len| {
                PACK_MAX_SCAN.min(len.saturating_sub(start as u64) as usize)
            });
            if scan
                .before(START_CODE_PREFIX_LEN as usize)
                .peek(want)
                .is_some_and(|pack| valid_pack(pack).is_some())
            {
                return None;
            }
        }

        if code == M4V_VIOS_CODE {
            if seen_vios {
                break;
            }
            // The search leaves the cursor on the octet carrying the value, so
            // a sequence header that opens the stream leaves it one prefix in.
            seen_vios_at_0 |= scan.offset() == START_CODE_PREFIX_LEN;
            seen_vios = true;
            scan.consume(M4V_VIOS_STEP);
            // `profile_and_level_indication` is the only thing between the
            // sequence header and what follows it, so the next start code is
            // exactly here.
            if next_start_code(&mut scan, 0).is_none() {
                break;
            }
            let Some(head) = scan.peek(1) else { break };
            code = head[0];
            // User data carries no length of its own, so it is crossed by
            // searching, and what the search lands on is read as the next code.
            if code == M4V_USER_DATA_CODE {
                if next_start_code(&mut scan, M4V_USER_DATA_SLACK).is_none() {
                    break;
                }
                let Some(head) = scan.peek(1) else { break };
                code = head[0];
            }
        }

        if code == M4V_VISUAL_OBJ_CODE {
            scan.consume(M4V_VISUAL_OBJ_STEP);
            if next_start_code(&mut scan, M4V_VISUAL_OBJ_SLACK).is_none() {
                break;
            }
            let Some(head) = scan.peek(1) else { break };
            code = head[0];
        }

        if code <= M4V_VIDEO_OBJ_CODE_MAX {
            if seen_vos {
                break;
            }
            seen_vos = true;
            scan.consume(M4V_VIDEO_OBJ_STEP);
            continue;
        }
        if (M4V_VIDEO_LAYER_CODE_MIN..=M4V_VIDEO_LAYER_CODE_MAX).contains(&code) {
            seen_vol = true;
            scan.consume(M4V_VIDEO_LAYER_STEP);
            continue;
        }
        if code == M4V_VOP_CODE {
            vops += 1;
            scan.consume(M4V_VOP_STEP);
            continue;
        }
        // A start code the format gives no meaning to here is evidence about
        // nothing, and the walk goes on from where it stands.
    }

    mpeg4_video_probability(vops, seen_vios_at_0, seen_vios, seen_vos, seen_vol)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::helpers::probe_engine::probe;

    /// An MPEG-2 pack header built from Table 2-39: `'01'` and the marker in
    /// octet 4, the three `marker_bit`s closing the clock reference runs in
    /// octets 6, 8 and 9, the two `marker_bit`s after `program_mux_rate` in
    /// octet 12, and `pack_stuffing_length` zero in octet 13. The clock and mux
    /// rate fields hold whatever keeps those markers in place and mean nothing
    /// to these cases.
    const PACK2: [u8; 14] = [
        0x00, 0x00, 0x01, 0xBA, 0x4C, 0x00, 0x04, 0x00, 0x04, 0x01, 0x00, 0x00, 0x03, 0x00,
    ];
    /// An MPEG-1 pack header: its fifth octet opens with `'0010'` and the
    /// header is 12 octets long.
    const PACK1: [u8; 12] = [
        0x00, 0x00, 0x01, 0xBA, 0x21, 0x00, 0x01, 0x00, 0x01, 0x80, 0x26, 0x25,
    ];
    /// A start code that names neither a pack, a system header nor a stream.
    const NOT_A_PACKET: [u8; 4] = [0x00, 0x00, 0x01, 0x42];
    /// Three octets: enough that a header ending the stream is not asked what
    /// follows it, not enough to begin another start code.
    const TAIL: [u8; 3] = [0xFF, 0xFF, 0xFF];

    /// The shortest system header: a `header_length` of 6, which is what the
    /// fixed fields of Table 2-40 occupy.
    fn system_header() -> Vec<u8> {
        vec![0x00, 0x00, 0x01, 0xBB, 0x00, 0x06, 0, 0, 0, 0, 0, 0]
    }

    /// A PES packet carrying `payload` octets, so that `PES_packet_length`
    /// counts the octets after that field.
    fn pes(payload: usize) -> Vec<u8> {
        let mut packet = vec![0x00, 0x00, 0x01, 0xE0, 0, payload as u8];
        packet.resize(6 + payload, 0xDD);
        packet
    }

    fn stream(parts: &[&[u8]]) -> Vec<u8> {
        let mut data = Vec::new();
        for part in parts {
            data.extend_from_slice(part);
        }
        data
    }

    /// A stream padded out to the 64 octets a probe would examine in a file of
    /// the size these cases are written at, so a golden value here is the value
    /// the same bytes get when read from a file.
    fn window(parts: &[&[u8]]) -> Vec<u8> {
        let mut data = stream(parts);
        data.resize(64, 0xFF);
        data
    }

    /// Table 2-39 places the fixed bits of an MPEG-2 pack header on octets 4,
    /// 6, 8, 9 and 12. Clearing any one of them is a header the format cannot
    /// produce.
    #[test]
    fn mpeg2_pack_marker_bits_are_all_required() {
        assert_eq!(valid_pack(&stream(&[&PACK2, &TAIL])), Some((14, 2)));
        for (index, mask) in [(4, 0xC4u8), (6, 0x04), (8, 0x04), (9, 0x01), (12, 0x03)] {
            let mut pack = PACK2;
            pack[index] &= !mask;
            assert_eq!(
                valid_pack(&stream(&[&pack, &TAIL])),
                None,
                "octet {index} mask {mask:#04x}"
            );
        }
        // Octet 12 carries two marker bits, so one of the two is not enough.
        for octet in [0x00u8, 0x01, 0x02] {
            let mut pack = PACK2;
            pack[12] = octet;
            assert_eq!(
                valid_pack(&stream(&[&pack, &TAIL])),
                None,
                "octet12 {octet}"
            );
        }
    }

    /// The octets after a pack header have to open another start code, which is
    /// what separates a real pack from a `00 00 01 BA` inside a video frame.
    /// A stream that ends before that point cannot be asked, and is taken.
    #[test]
    fn a_pack_is_followed_by_a_start_code_when_the_stream_reaches_it() {
        assert_eq!(valid_pack(&stream(&[&PACK2, &[0xFF; 4]])), None);
        assert_eq!(valid_pack(&stream(&[&PACK2, &TAIL])), Some((14, 2)));
        assert_eq!(valid_pack(&stream(&[&PACK2, &NOT_A_PACKET])), Some((14, 2)));
    }

    /// `pack_stuffing_length` is 3 bits, so up to 7 octets follow the header
    /// before the next one, and they are counted into the pack's length whether
    /// or not the stream is long enough to inspect what comes after them.
    #[test]
    fn pack_stuffing_is_counted_into_the_pack_length() {
        for stuff in 0..=7u8 {
            let mut pack = PACK2.to_vec();
            pack[13] = stuff;
            pack.resize(14 + stuff as usize, 0xFF);
            assert_eq!(
                valid_pack(&stream(&[&pack, &NOT_A_PACKET])),
                Some((14 + stuff as usize, 2)),
                "stuffing {stuff}"
            );
            assert_eq!(
                valid_pack(&pack),
                Some((14 + stuff as usize, 2)),
                "stuffing {stuff} with nothing after it"
            );
        }
    }

    /// A pack header shorter than the shortest one the format has is not one,
    /// and neither is an octet 4 that opens with neither `'01'` nor `'0010'`.
    #[test]
    fn a_pack_needs_its_own_length_and_a_known_form() {
        assert_eq!(valid_pack(&PACK2[..11]), None);
        assert_eq!(valid_pack(&PACK2[..13]), None);
        assert_eq!(valid_pack(&PACK1[..11]), None);
        for octet in [0x00u8, 0x40, 0x30, 0xF0] {
            let mut pack = PACK2;
            pack[4] = octet;
            assert_eq!(valid_pack(&stream(&[&pack, &TAIL])), None, "octet4 {octet}");
        }
    }

    /// The MPEG-1 pack header is 12 octets long and carries its own marker
    /// pattern; it is what makes a stream MPEG-1 rather than MPEG-2.
    #[test]
    fn mpeg1_pack_is_recognised_and_measured() {
        assert_eq!(valid_pack(&stream(&[&PACK1, &TAIL])), Some((12, 1)));
        assert_eq!(valid_pack(&stream(&[&PACK1, &NOT_A_PACKET])), Some((12, 1)));
        assert_eq!(valid_pack(&stream(&[&PACK1, &[0xFF; 4]])), None);
        for index in [6, 8, 9, 11] {
            let mut pack = PACK1;
            pack[index] = 0x00;
            assert_eq!(valid_pack(&stream(&[&pack, &TAIL])), None, "octet {index}");
        }
    }

    /// A program stream may not carry an unbounded PES packet, so a
    /// `PES_packet_length` of zero names nothing to step over.
    #[test]
    fn a_pes_packet_has_to_state_its_length() {
        assert_eq!(valid_pes(&stream(&[&pes(4), &TAIL])), Some(10));
        assert_eq!(valid_pes(&stream(&[&pes(0), &TAIL])), None);
        assert_eq!(valid_pes(&[0x00, 0x00, 0x01, 0xE0, 0x00]), None);
        assert_eq!(valid_pes(&stream(&[&pes(4), &[0xFF; 4]])), None);
        // The largest length the field holds is a length like any other.
        let mut packet = vec![0x00, 0x00, 0x01, 0xE0, 0xFF, 0xFF];
        assert_eq!(valid_pes(&packet), Some(6 + 65535));
        packet.resize(6 + 65535, 0xDD);
        assert_eq!(valid_pes(&packet), Some(6 + 65535));
        packet.extend_from_slice(&[0xDD; 4]);
        assert_eq!(valid_pes(&packet), None);
        packet.truncate(6 + 65535);
        packet.extend_from_slice(&NOT_A_PACKET);
        assert_eq!(valid_pes(&packet), Some(6 + 65535));
    }

    /// Table 2-40: `header_length` counts the octets after that field, and the
    /// fields it introduces are 6 octets before the stream loop begins, so a
    /// shorter header is not one.
    #[test]
    fn a_system_header_has_to_hold_its_own_fields() {
        let header = system_header();
        assert_eq!(valid_system_header(&stream(&[&header, &TAIL])), Some(12));
        assert_eq!(
            valid_system_header(&stream(&[&header, &NOT_A_PACKET])),
            Some(12)
        );
        assert_eq!(valid_system_header(&stream(&[&header, &[0xFF; 4]])), None);
        for length in [0u16, 1, 5] {
            let mut short = vec![0x00, 0x00, 0x01, 0xBB, (length >> 8) as u8, length as u8];
            short.resize(6 + length as usize, 0);
            assert_eq!(
                valid_system_header(&stream(&[&short, &TAIL])),
                None,
                "length {length}"
            );
        }
    }

    /// Table 2-5 reserves an `adaptation_field_control` of `'00'`, so a header
    /// only counts when it has payload or an adaptation field. A null packet,
    /// PID `0x1FFF`, is accepted with `'00'` as leniency for muxers that get it
    /// wrong; 2.4.3.3 says such a packet shall carry `'01'` and shall be
    /// discarded.
    #[test]
    fn a_transport_packet_header_needs_sync_and_a_payload() {
        for afc in [0x10u8, 0x20, 0x30] {
            assert!(ts_header(&[0x47, 0x10, 0x00, afc]), "afc {afc:#04x}");
        }
        assert!(!ts_header(&[0x47, 0x10, 0x00, 0x00]));
        assert!(ts_header(&[0x47, 0x1F, 0xFF, 0x00]));
        // One bit short of a null PID is not one.
        assert!(!ts_header(&[0x47, 0x1F, 0xFE, 0x00]));
        assert!(!ts_header(&[0x47, 0x1E, 0xFF, 0x00]));
        // transport_error_indicator is the top bit of the second octet.
        assert!(!ts_header(&[0x47, 0x90, 0x00, 0x30]));
        // The sync byte is 0x47 and nothing else.
        assert!(!ts_header(&[0x46, 0x10, 0x00, 0x30]));
        assert!(!ts_header(&[0x4F, 0x10, 0x00, 0x30]));
    }

    /// One transport packet header with payload to show for it, at the one
    /// size the cases below build streams at.
    fn ts_packet_head() -> [u8; TS_HEADER_SIZE] {
        [0x47, 0x50, 0x00, 0x10]
    }

    /// `n` packet headers spaced at `stride` octets, the bytes between them
    /// holding no sync byte. The probe cannot tell these from the front of a
    /// real stream of that size; nothing behind the last header is counted.
    fn ts_heads(n: u32, stride: i32) -> Vec<u8> {
        let mut stream = vec![0x00u8; (n as usize - 1) * stride as usize + TS_HEADER_SIZE];
        for i in 0..n as usize {
            stream[i * stride as usize..][..TS_HEADER_SIZE].copy_from_slice(&ts_packet_head());
        }
        stream
    }

    /// A probe-level walk, which is where the last header a stride can count
    /// is settled: the walk stops at the first offset without a header in
    /// view, and the probability follows the count one tenth to the header.
    #[test]
    fn transport_headers_count_one_tenth_each_up_to_the_cap() {
        let count = |n| {
            probe(&ts_heads(n, TS_PACKET_SIZES[0]), |tf| {
                mpeg_ts_probe(tf).map(|(p, size)| (p.get(), size))
            })
        };
        // One short of the run that is a stream at all, then the run, the cap,
        // and a stream long enough to reach it.
        assert_eq!(count(TS_MIN_HEADERS - 1), None);
        assert_eq!(count(TS_MIN_HEADERS), Some((10 * TS_MIN_HEADERS, 188)));
        assert_eq!(count(TS_MAX_HEADERS), Some((Prob::MAXIMUM.get(), 188)));
        assert_eq!(count(TS_MAX_HEADERS + 4), Some((Prob::MAXIMUM.get(), 188)));
    }

    /// A capture can begin in the middle of a packet, and the first stride
    /// that fills is answered from wherever it starts.
    #[test]
    fn transport_headers_are_found_at_an_offset_and_at_their_own_stride() {
        let at = |lead: usize| {
            let mut data = vec![0x00u8; lead];
            data.extend(ts_heads(TS_MIN_HEADERS, TS_PACKET_SIZES[0]));
            probe(&data, |tf| {
                mpeg_ts_probe(tf).map(|(p, size)| (p.get(), size))
            })
        };
        assert_eq!(at(7), Some((40, 188)));
        // The same headers at the same offset, spaced 204 apart.
        let mut data = vec![0x00u8; 7];
        data.extend(ts_heads(TS_MIN_HEADERS, 204));
        assert_eq!(
            probe(&data, |tf| mpeg_ts_probe(tf)
                .map(|(p, size)| (p.get(), size))),
            Some((40, 204))
        );
    }

    /// Headers short of the cap are counted to the last one the stream
    /// reaches: the confidence says how many were seen, not that the walk ran
    /// out of stream rather than of headers.
    #[test]
    fn a_transport_stream_short_of_the_cap_is_still_a_stream() {
        assert_eq!(
            probe(&ts_heads(TS_MIN_HEADERS, TS_PACKET_SIZES[0]), |tf| {
                mpeg_ts_probe(tf).map(|(p, size)| (p.get(), size))
            }),
            Some((40, 188))
        );
        assert_eq!(
            probe(&ts_heads(TS_MIN_HEADERS + 1, TS_PACKET_SIZES[0]), |tf| {
                mpeg_ts_probe(tf).map(|(p, size)| (p.get(), size))
            }),
            Some((50, 188))
        );
    }

    /// The rungs of the video ladder, from a full sequence down to a lone
    /// sequence header.
    #[test]
    fn video_confidence_falls_as_the_stream_gets_thinner() {
        let prob =
            |paired, at_0, seq, gop| video_probability(paired, at_0, seq, gop).map(|p| p.get());
        for paired in [6u32, 20] {
            assert_eq!(prob(paired, true, true, true), Some(98));
            assert_eq!(prob(paired, false, true, true), Some(98));
            assert_eq!(prob(paired, true, true, false), Some(90));
            assert_eq!(prob(paired, false, true, false), Some(90));
            assert_eq!(prob(paired, false, false, false), Some(80));
        }
        assert_eq!(prob(3, true, true, true), Some(70));
        assert_eq!(prob(3, false, true, true), Some(60));
        assert_eq!(prob(1, true, true, false), Some(50));
        assert_eq!(prob(1, false, true, false), Some(45));
        assert_eq!(prob(1, false, false, false), Some(40));
        assert_eq!(prob(0, false, true, false), Some(30));
        // Two pictures is not several, and a stream with neither a picture nor
        // a sequence header is not this format.
        assert_eq!(prob(2, true, true, true), Some(50));
        assert_eq!(prob(0, false, false, false), None);
        assert_eq!(prob(0, true, false, true), None);
    }

    /// Pack, system header and PES packets following one another is what makes a
    /// program stream. Every start code seen is one the format defines and the
    /// first is at the very start, so both bonuses apply.
    #[test]
    fn a_run_of_recognised_headers_is_a_program_stream() {
        let data = stream(&[&PACK2, &pes(8), &pes(8), &TAIL]);
        assert_eq!(sys_scan(&data), Some((90, 2)));
        // The same run with an MPEG-1 pack reads as MPEG-1.
        let data = stream(&[&PACK1, &pes(8), &pes(8), &TAIL]);
        assert_eq!(sys_scan(&data), Some((90, 1)));
        // A system header counts with the packs and is stepped over whole.
        let data = stream(&[&PACK2, &system_header(), &pes(8), &TAIL]);
        assert_eq!(sys_scan(&data), Some((90, 2)));
    }

    /// A stream that does not begin with its first packet is worth ten points
    /// less, which is how a capture joined mid-stream is held against a file.
    #[test]
    fn a_first_packet_not_at_the_start_costs_confidence() {
        let data = stream(&[&[0xFF; 6], &PACK2, &pes(8), &pes(8), &TAIL]);
        assert_eq!(sys_scan(&data), Some((80, 2)));
    }

    /// A start code the format does not define for a program stream does more
    /// than cost confidence: it sits far enough from the packet after it to
    /// break the run, so the same packets are a program stream on one side of
    /// it and not a program stream on the other.
    #[test]
    fn an_unrecognised_start_code_breaks_the_run() {
        let plain = window(&[&PACK2, &pes(8), &pes(8), &pes(8)]);
        assert_eq!(sys_scan(&plain), Some((80, 2)));
        let broken = window(&[&PACK2, &NOT_A_PACKET, &pes(8), &pes(8), &pes(8)]);
        assert_eq!(sys_scan(&broken), None);
    }

    /// Pack headers are optional in a program stream but PES packets are not:
    /// without one the stream is not this format, however many packs it has.
    #[test]
    fn packs_without_a_pes_packet_are_not_a_program_stream() {
        let data = stream(&[&PACK2, &PACK2, &PACK2, &TAIL]);
        assert_eq!(sys_scan(&data), None);
    }

    /// Only headers that sit close to the previous one make a run: a gap of more
    /// than four octets between one start code and the next restarts the count,
    /// so the packets after a gap do not add to the ones before it.
    #[test]
    fn headers_separated_by_a_gap_do_not_add_up() {
        let joined = window(&[&PACK2, &pes(8), &pes(8), &pes(8)]);
        assert_eq!(sys_scan(&joined), Some((80, 2)));
        let gapped = window(&[&PACK2, &pes(8), &[0xFF; 6], &pes(8), &pes(8)]);
        assert_eq!(sys_scan(&gapped), None);
    }

    /// A run of which some are PES packets is settled once it passes
    /// [`SYS_MAX_HEADERS`], and the walk stops there rather than reading on.
    /// Everything behind the boundary is therefore unseen, including an MPEG-1
    /// pack that would otherwise say which version the stream is. Six PES
    /// packets is 50 + 60 capped at 100, and every start code the walk reached
    /// is one the format defines.
    #[test]
    fn a_long_run_is_settled_at_the_header_cap() {
        let data = stream(&[
            &pes(8),
            &pes(8),
            &pes(8),
            &pes(8),
            &pes(8),
            &pes(8),
            &PACK1,
            &TAIL,
        ]);
        assert_eq!(sys_scan(&data), Some((100, 2)));
    }

    /// The version comes out of a header already known to be a pack. An octet 4
    /// that opens with `'0010'` but whose marker bits do not line up is not one
    /// the format can produce, so it neither counts nor calls the stream
    /// MPEG-1; the PES packets behind it make it MPEG-2.
    #[test]
    fn a_half_pack_does_not_decide_the_version() {
        let mut half_pack = PACK1;
        half_pack[6] = 0x00;
        assert_eq!(valid_pack(&stream(&[&half_pack, &TAIL])), None);

        let data = stream(&[&half_pack, &pes(8), &pes(8), &pes(8), &TAIL]);
        assert_eq!(sys_scan(&data), Some((80, 2)));
    }

    /// The top rung of the MPEG-4 visual ladder. Shared so a walk test can
    /// state it reaches the same value the pure ladder gives.
    const M4V_TOP: u32 = 99;

    /// The rungs of the MPEG-4 visual ladder, from a stream that showed
    /// everything down to one that showed only a video object layer header.
    #[test]
    fn mpeg4_video_confidence_falls_as_the_stream_gets_thinner() {
        let prob = |vops, at_0, vios, vos, vol| {
            mpeg4_video_probability(vops, at_0, vios, vos, vol).map(|p| p.get())
        };
        assert_eq!(prob(6, true, true, true, true), Some(M4V_TOP));
        assert_eq!(prob(6, false, true, true, true), Some(98));
        // A sequence header at the very start and a full run of pictures are
        // worth the same when the other is missing.
        assert_eq!(prob(1, true, true, true, true), Some(93));
        assert_eq!(prob(6, false, false, true, true), Some(93));
        assert_eq!(prob(6, false, false, false, true), Some(90));
        assert_eq!(prob(6, false, false, false, false), Some(79));
        assert_eq!(prob(3, false, true, true, true), Some(71));
        assert_eq!(prob(1, false, true, true, true), Some(60));
        assert_eq!(prob(1, false, false, true, true), Some(50));
        assert_eq!(prob(1, false, false, false, false), Some(40));
        assert_eq!(prob(0, false, false, true, true), Some(30));
        // A layer header with no video object and no picture behind it matches
        // no rung, and a suggestion carrying no confidence would say nothing,
        // so nothing is suggested.
        assert_eq!(prob(0, false, false, false, true), None);
        // Short of both a picture and a layer header the stream is not this
        // format, however many other headers it carried.
        assert_eq!(prob(0, false, false, false, false), None);
        assert_eq!(prob(0, true, true, false, false), None);
    }

    /// The walk steps headers by fixed counts, and the video object header is
    /// stepped by one octet: the code octet carries the whole header, its
    /// `video_id` being five of its own bits. A step of two lands between the
    /// VOP headers this stream carries, so their start code goes unseen, the
    /// picture count comes up short of the full run, and the stream falls a
    /// rung.
    #[test]
    fn a_mpeg4_stream_is_read_from_a_video_object_behind_its_layer() {
        // Each stepped header is followed by exactly the octets its step
        // counts — the sequence header's `profile_and_level_indication`, the
        // user data block's one opaque octet, and the visual object's
        // `visual_object_type` with the one the step leaves the cursor on —
        // so the search that settles the next start code answers at its first
        // position. The layer header carries its own octets because nothing
        // looks past it but an open search.
        let mut data = vec![
            0x00,
            0x00,
            0x01,
            M4V_VIOS_CODE,
            0x00,
            0x00,
            0x00,
            0x01,
            M4V_USER_DATA_CODE,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x01,
            M4V_VISUAL_OBJ_CODE,
            0xF5,
            0x04,
            0x00,
            0x00,
            0x01,
            M4V_VIDEO_LAYER_CODE_MIN,
            0xF8,
            0xE0,
            0xA0,
            0x1C,
            0x04,
            0x00,
            0x00,
            0x01,
            0x01, // video object 1, id in the code octet
        ];
        // Pictures carry two octets of their header behind the code: a
        // back-to-back run puts the next prefix behind the step and out of
        // any search's reach, which no real picture header does.
        data.extend_from_slice(&[0x00, 0x00, 0x01, M4V_VOP_CODE, 0x00, 0x00].repeat(6));
        assert_eq!(
            probe(&data, |tf| mpeg4_video_probe(tf).map(|p| p.get())),
            Some(M4V_TOP)
        );
    }

    /// Both counts are compared against a boundary, and each side of it is a
    /// different rung: six pictures is a full run where five is not, and three
    /// is several where two is not.
    #[test]
    fn mpeg4_video_picture_counts_use_their_boundaries_exactly() {
        let bare =
            |vops| mpeg4_video_probability(vops, false, false, false, false).map(|p| p.get());
        assert_eq!(bare(1), Some(40));
        assert_eq!(bare(5), Some(40));
        assert_eq!(bare(6), Some(79));
        assert_eq!(bare(20), Some(79));

        let full_headers =
            |vops| mpeg4_video_probability(vops, false, true, true, true).map(|p| p.get());
        assert_eq!(full_headers(1), Some(60));
        assert_eq!(full_headers(2), Some(60));
        assert_eq!(full_headers(3), Some(71));
        assert_eq!(full_headers(5), Some(71));
        assert_eq!(full_headers(6), Some(98));
    }
}
