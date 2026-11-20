// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
// Copyright (C) 2026 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Typefinder registration table and entry point.

#[allow(unused, non_camel_case_types, non_upper_case_globals, non_snake_case)]
use crate::bindings;
use bindings::{GstPlugin, gboolean, gst_caps_from_string, gst_caps_unref, gst_type_find_register};

use std::ffi::{CStr, CString};
use std::num::NonZeroU64;
use std::ptr;
use std::sync::OnceLock;

use crate::audio::{
    AacFraming, Ac3Stream, aa_probe, aac_probe, ac3_probe, ac4_probe, dts_probe, mod_probe,
    mp3_probe, sbc_probe, wavpack_probe,
};
use crate::av1::av1_probe;
use crate::bindings::gpointer;
use crate::containers::{
    MXF_SCAN_MAX, matroska_probe, mxf_partition_pack, pxstr_probe, q3gp_probe, qt_probe, qtif_probe,
};
use crate::h26x::{h263_probe, h264_probe, h265_probe, h266_probe};
use crate::helpers::{
    Feature, Finder, Prob, Scanner, TypeFind, opt, start_with, suggest, typefinder, u16le, u32le,
};
use crate::mpeg::{mpeg_sys_probe, mpeg_ts_probe, mpeg_video_probe, mpeg4_video_probe};
use crate::scan::{Pattern, find_any};
use crate::subtitles;
use crate::xdg_mime::SharedMimeInfo;
use crate::xml::{XmlProbe, xml_probe};
#[allow(unused_imports)]
use bindings::{
    GST_RANK_MARGINAL as MARGINAL, GST_RANK_PRIMARY as PRIMARY, GST_RANK_SECONDARY as SECONDARY,
};

// SHN - audio/x-shorten
// Magic `ajkg` @ 0 or `SHNAMPSK` at file tail (-8).
extern "C" fn shn_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"ajkg") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-shorten");
    } else if let Some(data) = tf.peek_var(-8, 8)
        && data == *b"SHNAMPSK"
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-shorten");
    }
}

// SVX - audio/x-svx
// Magic `FORM` @ 0 + `8SVX`/`16SV` @ 8.
extern "C" fn svx_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<4>(0)
        && *data == *b"FORM"
        && let Some(fmt) = tf.peek::<4>(8)
        && (*fmt == *b"8SVX" || *fmt == *b"16SV")
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-svx");
    }
}

// DSF - audio/x-dsf
// Magic `DSD ` @ 0; u64 BE @ 4 != 28.
extern "C" fn dsf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[..4] == *b"DSD "
    {
        let mut size_bytes = [0u8; 8];
        size_bytes.copy_from_slice(&data[4..12]);
        let size = u64::from_be_bytes(size_bytes);
        if size != 28 {
            suggest!(tf, Prob::MAXIMUM, c"audio/x-dsf");
        }
    }
}

// AIFF - audio/x-aiff
// Magic `FORM` @ 0 + `AIFF`/`AIFC` @ 8.
extern "C" fn aiff_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<4>(0)
        && *data == *b"FORM"
        && let Some(fmt) = tf.peek::<4>(8)
        && (*fmt == *b"AIFF" || *fmt == *b"AIFC")
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-aiff");
    }
}

extern "C" fn tiff_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };

    let endian = if data == &[0x49, 0x49, 0x2a, 0x00] {
        bindings::G_LITTLE_ENDIAN
    } else if data == &[0x4d, 0x4d, 0x00, 0x2a] {
        bindings::G_BIG_ENDIAN
    } else {
        return;
    };
    suggest!(tf, Prob::MAXIMUM, c"image/tiff", [
        (c"endianness", i32 : endian as i32),
    ]);
}

extern "C" fn ar_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<32>(0)
        && data[..7] == *b"!<arch>"
        && data[7] == b'\n'
    {
        // First member header starts at offset 8; file name field (16 bytes) is @8-23.
        if data[8..24]
            .iter()
            .all(|&b| b.is_ascii_graphic() || b == b' ')
        {
            suggest!(tf, Prob::NEARLY_CERTAIN, c"application/x-ar");
        } else {
            suggest!(tf, Prob::POSSIBLE, c"application/x-ar");
        }
    }
}

/// The GUID of the ASF Header Object, as ASF specification section 10.1 lists it.
/// The Header Object exists to give an ASF file a well-known opening sequence, so
/// that GUID alone is enough to recognise the container.
const ASF_HEADER_GUID: [u8; 16] = [
    0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c,
];

/// MS-WMSP describes an `mms://` stream over HTTP as opening with a packet whose
/// payload is the ASF header. Its first octet is a frame marker of `0x24` under a
/// flag meaning the next packet follows at once, the next octet names it as a
/// header packet, and the little-endian length after them counts what follows,
/// covering the eight octet payload header and the payload. That payload is the
/// ASF Header Object then the fixed 50 octet head of the Data Object, so the GUID
/// sits at offset 12 and any conforming stream is well past the length bound.
fn mmsh_probe(tf: &mut TypeFind) -> bool {
    // Four octets of framing header, then an eight octet payload header, then the
    // ASF header itself. A packet declaring no more than that is not carrying one.
    const GUID_AT: usize = 4 + 8;
    const MIN_PACKET_LEN: usize = GUID_AT + ASF_HEADER_GUID.len();

    let Some(head) = tf.peek::<MIN_PACKET_LEN>(0) else {
        return false;
    };
    head.starts_with(&[0x24, b'H'])
        && u16le(&head[2..4]) > MIN_PACKET_LEN as u16
        && head[GUID_AT..] == ASF_HEADER_GUID
}

extern "C" fn mmsh_type_find(tf: &mut TypeFind, _private: gpointer) {
    if mmsh_probe(tf) {
        suggest!(tf, Prob::LIKELY, c"application/x-mmsh");
    }
}

extern "C" fn cmml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<10>(0)
        && data[..8] == *b"CMML\0\0\0\0"
        && u16le(&data[8..]) >= 2
    {
        suggest!(tf, Prob::MAXIMUM, c"text/x-cmml");
    }
}

// VCD - video/x-vcd
// MPEG-1 disc image magic at offset 0.
extern "C" fn vcd_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[0] == 0x00
        && (1..11).all(|i| data[i] == 0xff)
        && data[11] == 0x00
    {
        suggest!(tf, Prob::MAXIMUM, c"video/x-vcd");
    }
}

// Helper for RIFF variants
fn is_riff_variant(tf: &mut TypeFind, fourcc: &[u8; 4]) -> bool {
    let Some(data) = tf.peek::<12>(0) else {
        return false;
    };
    data[..4] == *b"RIFF" && data[8..12] == *fourcc
}

extern "C" fn avi_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"AVI ") {
        suggest!(tf, Prob::MAXIMUM, c"video/x-msvideo");
    } else if let Some(data) = tf.peek::<12>(0)
        && data[..4] == *b"AVF0"
        && data[8..12] == *b"AVI "
    {
        // Handle some old school recorders which were essentially AVI, but
        // where `RIFF` was replaced by `AVF0`
        suggest!(tf, Prob::MAXIMUM, c"video/x-msvideo");
    }
}

extern "C" fn wav_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"WAVE") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-wav");
    }
}

extern "C" fn webp_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"WEBP") {
        suggest!(tf, Prob::MAXIMUM, c"image/webp");
    }
}

extern "C" fn fourxm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"4XMV") {
        suggest!(tf, Prob::MAXIMUM, c"video/x-4xm");
    }
}

extern "C" fn qcp_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"QLCM") {
        suggest!(tf, Prob::MAXIMUM, c"audio/qcelp");
    }
}

extern "C" fn cdxa_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"CDXA") {
        suggest!(tf, Prob::MAXIMUM, c"video/x-cdxa");
    }
}

extern "C" fn riff_mid_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"RMID") {
        suggest!(tf, Prob::MAXIMUM, c"audio/riff-midi");
    }
}

extern "C" fn xwma_type_find(tf: &mut TypeFind, _private: gpointer) {
    if is_riff_variant(tf, b"XWMA") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-xwma");
    }
}

// Y4M - application/x-yuv4mpeg
// "YUV4MPEG2 " includes the trailing space; the version is always 2.
extern "C" fn y4m_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"YUV4MPEG2 ") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-yuv4mpeg", [
            (c"y4mversion", i32 : 2),
        ]);
    }
}

// CD stream with XA sector headers - application/x-pxstr
extern "C" fn pxstr_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = pxstr_probe(tf) {
        suggest!(tf, probability, c"application/x-pxstr");
    }
}

// DVD image (ISO 9660) - application/octet-stream
// ECMA-119 puts the Primary Volume Descriptor in sector 16: a one type
// octet and `CD001` at 0x8000. The standard lets a file fill the system
// area before it; the finder insists it is all zero, a heuristic beyond
// the standard that keeps the claim off any file that merely carries an
// ISO 9660 volume behind other data.
const DVD_PVD_OFFSET: usize = 0x8000;
const DVD_SCAN_WINDOW: usize = 4096;

fn dvdiso_iso9660(tf: &mut TypeFind) -> bool {
    // The descriptor first: a foreign stream should not pay for a scan
    // through its first thirty-two KiB before being dismissed.
    let mut pvd = Scanner::new(tf, DVD_PVD_OFFSET as i64);
    let Some(descriptor) = pvd.peek(6) else {
        return false;
    };
    if descriptor != b"\x01CD001" {
        return false;
    }
    // The system area, zero to the octet the descriptor starts at.
    let mut scan = Scanner::new(tf, 0);
    let mut left = DVD_PVD_OFFSET;
    while left > 0 {
        let want = left.min(DVD_SCAN_WINDOW);
        let Some(area) = scan.peek(want) else {
            return false;
        };
        if area.iter().any(|&b| b != 0) {
            return false;
        }
        scan.consume(want);
        left -= want;
    }
    true
}

extern "C" fn dvdiso_type_find(tf: &mut TypeFind, _private: gpointer) {
    if dvdiso_iso9660(tf) {
        suggest!(tf, Prob::LIKELY, c"application/octet-stream");
    }
}

// =============================================================================
// Complex finder - BMP (DIB structure validation)
// =============================================================================

// BMP - image/bmp
// Magic "BM" @0, DIB header validation at offsets 14-28/32.
extern "C" fn bmp_type_find(tf: &mut TypeFind, _private: gpointer) {
    const TEN_MIB: u32 = 10 * 1024 * 1024;
    const MAX_DIM: u32 = 0xFFFFF;

    let Some(data) = tf.peek::<32>(0) else { return };

    if data[0] != b'B' || data[1] != b'M' || data[6..10].iter().any(|&b| b != 0) {
        return;
    }
    let pixel_offset = u32le(&data[10..]);
    let header_size = u32le(&data[14..]);
    if pixel_offset > TEN_MIB {
        return;
    }

    let (w, h, planes, bpp) = match header_size {
        // BITMAPCOREHEADER - w,h as u16.
        0x0C => (
            u32::from(u16le(&data[18..])),
            u32::from(u16le(&data[20..])),
            u16le(&data[22..]),
            u16le(&data[24..]),
        ),
        // BITMAPINFOHEADER V3-V4 - w,h as u32.
        40 | 64 | 108 | 124 | 0xF0 => (
            u32le(&data[18..]),
            u32le(&data[22..]),
            u16le(&data[26..]),
            u16le(&data[28..]),
        ),
        _ => return,
    };

    if w == 0 || h == 0 || w > MAX_DIM || h > MAX_DIM || planes != 1 {
        return;
    }
    if !matches!(bpp, 1 | 4 | 8 | 16 | 24 | 32) {
        return;
    }

    suggest!(tf, Prob::MAXIMUM,
             c"image/bmp",
             [ (c"width", i32 : w as i32),
               (c"height", i32 : h as i32),
               (c"bpp", i32 : bpp as i32), ]);
}

// PNG - image/png
// Signature @0, then walk chunk headers (length u32 BE + fourcc + payload
// + 4 CRC bytes): acTL/PLTE raise the confidence, IDAT settles it.
extern "C" fn png_type_find(tf: &mut TypeFind, _private: gpointer) {
    const PNG_MAGIC: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];
    if !tf.starts_with(&PNG_MAGIC) {
        return;
    }

    let mut scan = Scanner::new(tf, 8); // after the signature
    let mut prob = Prob::LIKELY; // sig match is baseline confidence
    let mut animated = false;

    for _ in 0..25 {
        let Some(chunk_header) = scan.peek(8) else {
            break;
        };
        let chunk_data_len = u32::from_be_bytes([
            chunk_header[0],
            chunk_header[1],
            chunk_header[2],
            chunk_header[3],
        ]);
        match &chunk_header[4..8] {
            b"acTL" => {
                prob = Prob::NEARLY_CERTAIN;
                animated = true;
            }
            b"PLTE" => prob = Prob::NEARLY_CERTAIN,
            b"IDAT" => {
                prob = Prob::MAXIMUM;
                break;
            }
            _ => {}
        }
        scan.consume(usize::saturating_add(chunk_data_len as usize, 12));
    }
    if animated {
        suggest!(tf, prob, c"image/png", [(c"animated", bool : true),]);
    } else {
        suggest!(tf, prob, c"image/png");
    }
}

// JPEG - image/jpeg
// SOI @0, then walk marker segments (FF xx, length u16 BE, payload).
// APP0 JFIF / APP1 Exif and a SOF with nonzero dimensions raise confidence.
extern "C" fn jpeg_type_find(tf: &mut TypeFind, _private: gpointer) {
    const MAX_WALK: i64 = 200 * 1024;
    if !tf.starts_with(&[0xff, 0xd8]) {
        return;
    }

    let mut scan = Scanner::new(tf, 2); // after the SOI
    let mut markers = 1; // the SOI counts as a marker
    let mut app_found = false;
    let mut fields: Option<(u32, u32, u8)> = None;

    // Nine bytes cover the header (FF xx, length), the APP0/APP1
    // signature and the SOF dimensions.
    while scan.offset() < MAX_WALK {
        let Some(seg) = scan.peek(9) else {
            break;
        };
        if seg[0] != 0xff {
            break;
        }
        let marker = seg[1];
        // TEM, RST0-7, SOI and EOI carry no length.
        if marker == 0x01 || (0xd0..=0xd9).contains(&marker) {
            scan.consume(2);
            continue;
        }
        let seg_len = u16::from_be_bytes([seg[2], seg[3]]) as usize;

        match marker {
            // Fill bytes: skip one, re-check next iteration.
            0xff => {
                scan.consume(1);
                continue;
            }

            // Start Of Frame: extract dimensions from payload.
            m if (0xc0..=0xcf).contains(&m)
                && m != 0xc4
                && m != 0xc8
                && m != 0xcc
                && seg_len >= 10 =>
            {
                let height = u16::from_be_bytes([seg[5], seg[6]]) as u32;
                let width = u16::from_be_bytes([seg[7], seg[8]]) as u32;
                if width != 0 && height != 0 {
                    fields = Some((width, height, m & 0x0f));
                    break;
                }
            }

            // JFIF / Exif: detect from payload.
            m if matches!(m, 0xe0 | 0xe1)
                && seg_len >= 6
                && seg[4..8] == if m == 0xe0 { *b"JFIF" } else { *b"Exif" } =>
            {
                app_found = true;
            }

            // Counted segments (DHT/DAC/DQT/DRI/COM).
            0xc4 | 0xcc | 0xdb | 0xdd | 0xfe => markers += 1,

            // Generic APPn: count but don't inspect payload.
            m if (0xe0..=0xef).contains(&m) => markers += 1,

            _ => {} // unknown marker: skip and keep walking
        }
        scan.consume(2 + seg_len);
    }

    let Some((width, height, sof_marker)) = fields else {
        // With no dimensions to report an APP marker is the only thing to rate.
        let prob = if app_found {
            Prob::MAXIMUM
        } else {
            Prob::POSSIBLE
        };
        suggest!(tf, prob, c"image/jpeg");
        return;
    };
    let prob = if markers >= 5 || app_found {
        Prob::MAXIMUM
    } else {
        Prob::LIKELY
    };
    suggest!(tf, prob, c"image/jpeg", [
        (c"width", i32 : width as i32),
        (c"height", i32 : height as i32),
        (c"sof-marker", i32 : sof_marker as i32),
    ]);
}

// BRSTM - audio/x-brstm
// `RSTM` @ 0 + u32 LE @ 4 in {0xFFFE, 0xFEFF}.
extern "C" fn brstm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<8>(0)
        && data[..4] == *b"RSTM"
        && matches!(u32le(&data[4..]), 0xFFFE | 0xFEFF)
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-brstm");
    }
}

// BFSTM - audio/x-bfstm
// `FSTM`/`CSTM` @ 0 + u32 LE @ 4 in {0xFFFE, 0xFEFF}.
extern "C" fn bfstm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<8>(0)
        && (data[..4] == *b"FSTM" || data[..4] == *b"CSTM")
        && matches!(u32le(&data[4..]), 0xFFFE | 0xFEFF)
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-bfstm");
    }
}

// PVA - video/x-pva
// `AV` @ 0, byte[2] < 3, byte[4] == 0x55.
extern "C" fn pva_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<5>(0)
        && data[..2] == *b"AV"
        && data[2] < 3
        && data[4] == 0x55
    {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"video/x-pva");
    }
}

// Film CPK - video/x-film-cpk
// `FILM` @ 0 + `FDSC` @ 16.
extern "C" fn film_cpk_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<20>(0)
        && data[..4] == *b"FILM"
        && data[16..20] == *b"FDSC"
    {
        suggest!(tf, Prob::MAXIMUM, c"video/x-film-cpk");
    }
}

// ID3v2 - application/x-id3
// `ID3` @ 0; bytes 3-4 not 0xFF; top bit clear in size bytes 6-9.
extern "C" fn id3v2_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<10>(0)
        && data[..3] == *b"ID3"
        && data[3] != 0xFF
        && data[4] != 0xFF
        && data[6] & 0x80 == 0
        && data[7] & 0x80 == 0
        && data[8] & 0x80 == 0
        && data[9] & 0x80 == 0
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-id3");
    }
}

// ID3v1 - application/x-id3
// `TAG` at -128 (file end).
extern "C" fn id3v1_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek_var(-128, 3)
        && data == *b"TAG"
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-id3");
    }
}

// APE tag - application/x-apetag
// `APETAGEX` @ 0 or at file tail (-32).
extern "C" fn apetag_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"APETAGEX") || tf.peek_var(-32, 8).is_some_and(|d| d == *b"APETAGEX") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-apetag");
    }
}

// M4A - audio/x-m4a
// `ftypM4A ` @ 4 (ISO base media).
extern "C" fn m4a_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<8>(4)
        && *data == *b"ftypM4A "
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-m4a");
    }
}

// Ogg Skeleton - application/x-ogg-skeleton
// `fishead\0` @ 0; u16 LE version == 3.0 at offsets 8-12.
extern "C" fn oggskel_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[..8] == *b"fishead\0"
        && u16le(&data[8..]) == 3
        && u16le(&data[10..]) == 0
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-ogg-skeleton", [
            (c"parsed", bool : false),
        ]);
    }
}

// PostScript - application/postscript
// Optional leading 0x04, then `%!`.
extern "C" fn postscript_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<3>(0) else {
        return;
    };
    // Optional leading 0x04
    if data
        .strip_prefix(b"\x04")
        .unwrap_or(data)
        .starts_with(b"%!")
    {
        suggest!(tf, Prob::POSSIBLE, c"application/postscript");
    }
}

// TAR - application/x-tar
// `ustar  \0` (old GNU, 8 bytes) or `ustar\0` + 2 digits @ offset 257.
extern "C" fn tar_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_var(257, 8) else {
        return;
    };
    // Old GNU: "ustar  \0" (8 bytes)
    let old_gnu = data == *b"ustar  \0";
    // New GNU: "ustar\0" + 2 ASCII digits
    let new_gnu =
        data.starts_with(b"ustar\0") && data[6].is_ascii_digit() && data[7].is_ascii_digit();
    if old_gnu || new_gnu {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"application/x-tar");
    }
}

// MXMF - audio/mobile-xmf
// `XMF_` @ 0, `2.00` @ 4, XmfFileTypeID (BE u32 @ 8) == 2
// (Mobile XMF; checked via the low byte @ 11, per RFC 4723).
extern "C" fn mxmf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[..8] == *b"XMF_2.00"
        && data[11] == 2
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/mobile-xmf");
    }
}

// OpenEXR version-field validity (the u32 after the magic):
// version (bits 0-7) is 1 or 2, and if bit 9 (tiled) is set,
// bits 11 (deep) and 12 (multipart) must be 0.
fn exr_version_field_ok(flags: u32) -> bool {
    ((flags & 0xff) == 1 || (flags & 0xff) == 2) && ((flags & 0x200) == 0 || (flags & 0x1800) == 0)
}

// EXR - image/x-exr
// u32 LE @ 0 == 0x01312f76 (magic), valid version field @ 4.
extern "C" fn exr_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<8>(0)
        && u32le(&data[0..4]) == 0x01312f76
        && exr_version_field_ok(u32le(&data[4..8]))
    {
        suggest!(tf, Prob::MAXIMUM, c"image/x-exr");
    }
}

// JPEG 2000 - image/jp2 or video/mj2
// 12-byte JP2 signature @ 0 + ftyp box with major brand.
extern "C" fn jp2_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<24>(0)
        && data[..12] == *b"\x00\x00\x00\x0cjP  \r\n\x87\n"
        && data[16..20] == *b"ftyp"
    {
        match &data[20..24] {
            b"jp2 " => suggest!(tf, Prob::MAXIMUM, c"image/jp2"),
            b"mjp2" => suggest!(tf, Prob::MAXIMUM, c"video/mj2"),
            _ => {}
        }
    }
}

// Open Timeline IO - JSON object with "OTIO_SCHEMA": as its first string key.
extern "C" fn otio_type_find(tf: &mut TypeFind, _private: gpointer) {
    // The first string key has to appear within the first 30 bytes.
    let Some(data) = tf.peek::<30>(0) else { return };

    // Split on '{' - if there is no brace this is not a JSON object.
    // The content after the first brace must start with "OTIO_SCHEMA":.
    if let Ok(text) = std::str::from_utf8(data)
        && let Some(after_brace) = text.split('{').nth(1)
        && after_brace.trim_start().starts_with(r#""OTIO_SCHEMA":"#)
    {
        suggest!(
            tf,
            Prob::MAXIMUM,
            c"application/vnd.pixar.opentimelineio+json"
        );
    }
}

// FLIC / FLC - video/x-fli
// Magic: byte[4] in {0x11, 0x12, 0x30, 0x44} and byte[5] == 0xAF.
extern "C" fn flx_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<6>(0) else { return };

    if !matches!(data[4], 0x11 | 0x12 | 0x30 | 0x44) || data[5] != 0xAF {
        return;
    }

    let Some(data) = tf.peek::<134>(0) else {
        suggest!(tf, Prob::LIKELY, c"video/x-fli");
        return;
    };

    if (data[132] == 0x00 || data[132] == 0xFA) && data[133] == 0xF1 {
        suggest!(tf, Prob::MAXIMUM, c"video/x-fli");
    }
}

// JPEG 2000 Code Stream (JPC) - image/x-jpc
// Requires SOC+SIZ @ 0; walk markers requiring SOT + COD + QCD.
extern "C" fn jpc_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };
    if data != &[0xFF, 0x4F, 0xFF, 0x51] {
        return;
    }

    // Marker codes we care about or skip.
    const SOT: u16 = 0xFF90;
    const COD: u16 = 0xFF52;
    const QCD: u16 = 0xFF5C;
    let skip_markers = [
        0xFF53, // COC
        0xFF5E, // RGN
        0xFF5D, // QCC
        0xFF5F, // POC
        0xFF57, // PLM
        0xFF60, // PPM
        0xFF55, // TLM
        0xFF63, // CRG
        0xFF64, // COM
        0xFF78, // CBD
        0xFF75, // MCC
        0xFF74, // MCT
        0xFF77, // MCO
    ];
    let is_skip = |m: u16| skip_markers.contains(&m);

    let mut offset: i64 = 4;
    let mut has_cod = false;
    let mut has_qcd = false;

    loop {
        let Some(seg) = tf.peek::<2>(offset) else {
            return;
        };
        let seg_len = u16::from_be_bytes([seg[0], seg[1]]) as i64;
        offset += seg_len; // length includes its own 2 bytes

        let Some(marker_data) = tf.peek::<2>(offset) else {
            return;
        };
        let marker = u16::from_be_bytes([marker_data[0], marker_data[1]]);
        match marker {
            SOT => break,
            COD => has_cod = true,
            QCD => has_qcd = true,
            _ if is_skip(marker) => {}
            _ => return, // unknown marker
        }
        offset += 2;
    }

    if has_cod && has_qcd {
        suggest!(tf, Prob::MAXIMUM, c"image/x-jpc");
    }
}

// Musepack - audio/x-musepack
// Magic: "MP+" with v7 check; "MPCK" for v8.
extern "C" fn musepack_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };

    if data[..3] == *b"MP+" {
        let prop = if (data[3] & 0x7F) == 7 {
            Prob::MAXIMUM
        } else {
            Prob::from(90)
        };
        suggest!(tf, prop, c"audio/x-musepack",
            [(c"streamversion", i32 : 7),]);
        return;
    }

    if data[..4] == *b"MPCK" {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-musepack",
            [(c"streamversion", i32 : 8),]);
    }
}

// DV - video/x-dv
// Magic: 0x1F 0x07 0x00 @ 0; PAL/NTSC from byte[3].
extern "C" fn dv_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<5>(0) else { return };

    if data[0] != 0x1F || data[1] != 0x07 || data[2] != 0x00 {
        return;
    }

    let pal = data[3] & 0x80 != 0;
    suggest!(tf, Prob::MAXIMUM, c"video/x-dv",
        [(c"systemstream", bool : true),
         (c"format", str : if pal { c"PAL" } else { c"NTSC" }),]);
}

// Kate - subtitle/x-kate or application/x-kate
// Magic: 0x80 "kate" 0x00 0x00 0x00 @ 0; category at @48.
extern "C" fn kate_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<64>(0) else { return };

    if !data.starts_with(b"\x80kate\x00\x00\x00") {
        return;
    }

    // Category occupies 15 bytes at offset 48, NUL-terminated.
    let raw = &data[48..63];
    let end = raw.iter().position(|&b| b == 0).unwrap_or(raw.len());
    let category = &raw[..end];
    if category == b"subtitles"
        || category == b"SUB"
        || category == b"spu-subtitles"
        || category == b"K-SPU"
    {
        suggest!(tf, Prob::MAXIMUM, c"subtitle/x-kate");
    } else {
        suggest!(tf, Prob::MAXIMUM, c"application/x-kate");
    }
}

// FLAC - audio/x-flac
// Magic: "fLaC" @ 0 or 0x7F "FLAC" 0x01 (FLAC-in-Ogg) @ 0.
extern "C" fn flac_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };

    if *data == *b"fLaC" {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-flac");
        return;
    }

    let Some(data) = tf.peek::<6>(0) else { return };

    if *data == *b"\x7FFLAC\x01" {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-flac");
    }
}

// SDP - application/sdp
// Starts with "v=0" followed by LF or CRLF.
fn is_sdp_header(data: &[u8]) -> bool {
    data.starts_with(b"v=0\n") || data.starts_with(b"v=0\r\n")
}

extern "C" fn sdp_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<5>(0)
        && is_sdp_header(data)
    {
        suggest!(tf, Prob::MAXIMUM, c"application/sdp");
    }
}

// EA - video/x-ea
// Magic: LE fourcc @ 0 in a known set; size u32 LE @ 4 in 8..=0xFFFFF
// (byte-swapped when the raw value exceeds 0xFFFFFF).
extern "C" fn ea_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<8>(0) else { return };
    const FOURCCS: [[u8; 4]; 10] = [
        *b"ISNh", *b"SCHl", *b"SEAD", *b"SHEN", *b"kVGT", *b"MADk", *b"MPCh", *b"MVhd", *b"MVIh",
        *b"AVP6",
    ];
    if !FOURCCS.contains(&[data[0], data[1], data[2], data[3]]) {
        return;
    }
    let size = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
    let size = if size > 0x00FF_FFFF {
        size.swap_bytes()
    } else {
        size
    };
    if (8..=0xFFFFF).contains(&size) {
        suggest!(tf, Prob::MAXIMUM, c"video/x-ea");
    }
}

// NSV - video/x-nsv
// Magic: "NSVf"/"NSVs" @ 0, or a sync frame "NSVs" @ i in the first 4 KiB.
// Sync frame header is 19 bytes; the bit-packed payload size fields at
// i+19..i+24 are num_aux (4 bits), vsize (20 bits) and asize (16 bits).
// The frame spans i..i+24+vsize+asize; a nonsync frame right after starts
// with the bytes EF BE. Bare marker suggests 1.
/// The sync frame marker, `NSVs`.
const NSV_SYNC: Pattern<4> = Pattern::new(*b"NSVs", [0xFF; 4]);

extern "C" fn nsv_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(4096) else {
        return;
    };

    if data.starts_with(b"NSVf") || data.starts_with(b"NSVs") {
        suggest!(tf, Prob::MAXIMUM, c"video/x-nsv");
        return;
    }
    // First "NSVs" chunk marker (offset 0 already handled above).
    if let Some(i) = find_any(&[NSV_SYNC], data)
        && data.len() >= i + 24
    {
        // Bit-packed sizes at i+19..i+24: low nibble of i+19 is num_aux,
        // so vsize is the 24-bit LE word shifted right by 4; asize is the
        // full 16-bit LE word at i+22.
        let vsize =
            (u32::from_le_bytes([data[i + 19], data[i + 20], data[i + 21], 0]) >> 4) as usize;
        let asize = u16::from_le_bytes([data[i + 22], data[i + 23]]) as usize;
        let offset = i + 24 + vsize + asize;
        if data.get(offset..offset + 2) == Some(&[0xEF, 0xBE]) {
            suggest!(tf, Prob::NEARLY_CERTAIN, c"video/x-nsv");
        } else {
            suggest!(tf, Prob::MINIMUM, c"video/x-nsv");
        }
    }
}

// NUT - application/x-nut
// Magic: 8-byte sync word "NM" 7A 56 1F 5F 04 AD anywhere in the first 4 KiB.
extern "C" fn nut_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(4096) else {
        return;
    };
    const MARKER: [u8; 8] = [0x4E, 0x4D, 0x7A, 0x56, 0x1F, 0x5F, 0x04, 0xAD];
    if data.windows(8).any(|w| w == MARKER) {
        suggest!(tf, Prob::MAXIMUM, c"application/x-nut");
    }
}

// Interplay MVE - application/x-ipmovie
// Magic: "Interplay MVE File\x1A\0\x1A\0" (22 bytes) anywhere in the first
// 4 KiB.
extern "C" fn ipmovie_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(4096) else {
        return;
    };
    const SIG: [u8; 22] = *b"Interplay MVE File\x1A\0\x1A\0";
    if data.windows(22).any(|w| w == SIG) {
        suggest!(tf, Prob::LIKELY, c"video/x-ipmovie");
    }
}

// SMAF - application/vnd.smaf
// Magic: "MMMD" @ 0 and "CNTI" @ 8.
extern "C" fn mmf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[..4] == *b"MMMD"
        && data[8..12] == *b"CNTI"
    {
        suggest!(tf, Prob::LIKELY, c"application/vnd.smaf");
    }
}

// IFF - application/x-iff
// Magic: "FORM" @ 0 with a known form type @ 8, or "FRM8" @ 0 with "DSD " @ 12.
extern "C" fn iff_type_find(tf: &mut TypeFind, _private: gpointer) {
    const FORM_TYPES: [[u8; 4]; 10] = [
        *b"8SVX", *b"16SV", *b"MAUD", *b"PBM ", *b"ACBM", *b"DEEP", *b"ILBM", *b"RGB8", *b"ANIM",
        *b"RGBN",
    ];
    let Some(data) = tf.peek::<16>(0) else { return };
    if (data[..4] == *b"FORM" && FORM_TYPES.contains(&[data[8], data[9], data[10], data[11]]))
        || (data[..4] == *b"FRM8" && data[12..16] == *b"DSD ")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-iff");
    }
}

// GXF - application/gxf
// Magic: 00 00 00 00 01 BC @ 0 and 00 00 00 00 E1 E2 @ 10.
extern "C" fn gxf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<16>(0)
        && data[..6] == *b"\x00\x00\x00\x00\x01\xBC"
        && data[10..16] == *b"\x00\x00\x00\x00\xE1\xE2"
    {
        suggest!(tf, Prob::MAXIMUM, c"application/gxf");
    }
}

// MXF - application/mxf
// Magic: the 13-octet header partition pack key, its pack type, a partition
// status below five and a reserved octet, anywhere in the first 64 KiB.
extern "C" fn mxf_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(MXF_SCAN_MAX) else {
        return;
    };
    if mxf_partition_pack(data) {
        suggest!(tf, Prob::MAXIMUM, c"application/mxf");
    }
}

// MS-DOS/PE executable - application/x-ms-dos-executable
// Magic: "MZ" @ 0, u16 LE @ 8 == 4, "PE" at the u32 LE offset @ 60.
extern "C" fn msdos_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<64>(0) else { return };
    if data[..2] != *b"MZ" || u16::from_le_bytes([data[8], data[9]]) != 4 {
        return;
    }
    let pe_offset = u32::from_le_bytes([data[60], data[61], data[62], data[63]]) as i64;
    if let Some(pe) = tf.peek::<2>(pe_offset)
        && pe == b"PE"
    {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"application/x-ms-dos-executable");
    }
}

// Speex - audio/x-speex
// Magic: "Speex   " @ 0; header size u32 LE @ 32 >= 80; rate u32 LE @ 36
// <= 48000; mode u32 LE @ 40 <= 3.
extern "C" fn speex_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<80>(0) else { return };
    if data[..8] != *b"Speex   " {
        return;
    }
    let header_size = u32::from_le_bytes([data[32], data[33], data[34], data[35]]);
    let rate = u32::from_le_bytes([data[36], data[37], data[38], data[39]]);
    let mode = u32::from_le_bytes([data[40], data[41], data[42], data[43]]);
    if header_size >= 80 && rate <= 48000 && mode <= 3 {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-speex");
    }
}

// Vorbis - audio/x-vorbis
// Magic: 0x01 "vorbis" + 4 zero bytes @ 0; channels @ 11 != 0; samplerate
// u32 LE @ 12 != 0; blocksize exponents @ 28 both in 6..=13 with bs0 <= bs1;
// framing bit @ 29 set.
extern "C" fn vorbis_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<30>(0)
        && data[..11] == *b"\x01vorbis\x00\x00\x00\x00"
        && data[11] != 0
        && u32::from_le_bytes([data[12], data[13], data[14], data[15]]) != 0
        && (data[29] & 0x01) == 1
    {
        let bs0 = data[28] & 0x0F;
        let bs1 = data[28] >> 4;
        if (6..=13).contains(&bs0) && (6..=13).contains(&bs1) && bs0 <= bs1 {
            suggest!(tf, Prob::MAXIMUM, c"audio/x-vorbis");
        }
    }
}

// Ogg/Annodex - application/ogg
// Walks BOS "OggS" pages in the first 4 KiB (1 segment, segment size >= 8)
// and classifies the first segment's payload against known codec markers.
#[derive(Clone, Copy)]
enum OggAnxKind {
    Audio,
    Video,
    Kate,
    Other,
    Ignored,
}

const OGGANX_MARKERS: [(&[u8], OggAnxKind); 18] = [
    (b"\x01vorbis", OggAnxKind::Audio),
    (b"\x80theora", OggAnxKind::Video),
    (b"fLaC", OggAnxKind::Audio),
    (b"\x7FFLAC", OggAnxKind::Audio),
    (b"Speex", OggAnxKind::Audio),
    (b"CMML\0\0\0\0", OggAnxKind::Other),
    (b"PCM     ", OggAnxKind::Audio),
    (b"Annodex", OggAnxKind::Ignored),
    (b"fishead", OggAnxKind::Ignored),
    (b"AnxData", OggAnxKind::Ignored),
    (b"CELT    ", OggAnxKind::Audio),
    (b"\x80kate\0\0\0", OggAnxKind::Kate),
    (b"BBCD\0", OggAnxKind::Video),
    (b"OVP80\x01\x01", OggAnxKind::Video),
    (b"OpusHead", OggAnxKind::Audio),
    (b"\x01audio\0\0\0", OggAnxKind::Audio),
    (b"\x01video\0\0\0", OggAnxKind::Video),
    (b"\x01text\0\0\0", OggAnxKind::Other),
];

extern "C" fn ogganx_type_find(tf: &mut TypeFind, _private: gpointer) {
    let mut scan = Scanner::new(tf, 0);
    let mut ogg_syncs = 0u32;
    let mut audio = 0u32;
    let mut video = 0u32;
    let mut kate = 0u32;
    let mut other = 0u32;

    while scan.offset() < 4096 {
        let Some(page) = scan.peek(64) else {
            break;
        };
        if &page[..5] != b"OggS\0" {
            break;
        }
        ogg_syncs += 1;
        if page[5] != 0x02 || page[26] != 1 {
            break;
        }
        let size = page[27] as usize;
        if size < 8 {
            break;
        }
        // 27-byte page header + lacing table.
        scan.consume(28);
        let Some(payload) = scan.peek(size) else {
            break;
        };
        let kind = OGGANX_MARKERS
            .iter()
            .find(|(marker, _)| payload.starts_with(marker))
            .map_or(OggAnxKind::Other, |(_, kind)| *kind);
        match kind {
            OggAnxKind::Audio => audio += 1,
            OggAnxKind::Video => video += 1,
            OggAnxKind::Kate => kate += 1,
            OggAnxKind::Ignored => {}
            OggAnxKind::Other => other += 1,
        }
        scan.consume(size);
    }

    if ogg_syncs == 0 {
        return;
    }

    let mime = if video > 0 {
        c"video/ogg"
    } else if audio > 0 {
        c"audio/ogg"
    } else if kate > 0 && other == 0 {
        c"application/kate"
    } else {
        c"application/ogg"
    };
    suggest!(tf, Prob::MAXIMUM, mime);
}

// IRCAM - audio/x-ircam
// Magic: masked match 64 A3 0? 00 (mask FF FF F8 FF) @ 0, forward or
// byte-reversed.
extern "C" fn ircam_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };
    const MATCH: [u8; 4] = [0x64, 0xA3, 0x00, 0x00];
    const MASK: [u8; 4] = [0xFF, 0xFF, 0xF8, 0xFF];
    let forward = (0..4).all(|x| data[x] & MASK[x] == MATCH[x]);
    let reversed = (0..4).all(|x| data[x] & MASK[3 - x] == MATCH[3 - x]);
    if forward || reversed {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-ircam");
    }
}

/// Peek window for the XML finders. The root tag normally sits in the first
/// few hundred bytes; 4 KiB leaves room for long prologs and comments.
const XML_WINDOW: usize = 4096;

/// Probe the first `XML_WINDOW` bytes of the stream for XML. The element
/// matchers below compare the returned names ignoring ASCII case.
fn xml_probe_typefinder(tf: &mut TypeFind) -> Option<XmlProbe<'_>> {
    tf.peek_at_most(XML_WINDOW).and_then(xml_probe)
}

// XML - application/xml
// Any root element, with a declaration.
extern "C" fn xml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(xml) = xml_probe_typefinder(tf)
        && xml.has_prolog
    {
        suggest!(tf, Prob::MINIMUM, c"application/xml");
    }
}

// DASH MPD - application/dash+xml
// Element matcher on "mpd".
extern "C" fn dash_mpd_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("mpd")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/dash+xml");
    }
}

// GES - application/xges
// Element matcher on "ges".
extern "C" fn xges_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("ges")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/xges");
    }
}

// FCPXML - application/vnd.apple-fcp+xml
// Element matcher on "fcpxml".
extern "C" fn fcpxml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("fcpxml")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/vnd.apple-fcp+xml");
    }
}

// XMEML - application/vnd.apple-xmeml+xml
// Element matcher on "xmeml".
extern "C" fn xmeml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("xmeml")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/vnd.apple-xmeml+xml");
    }
}

// SMIL - application/smil
// Element matcher on "smil".
extern "C" fn smil_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("smil")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/smil");
    }
}

// TTML - application/ttml+xml
// Element matcher on "tt".
extern "C" fn ttml_xml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.root_is("tt")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/ttml+xml");
    }
}

// HTML - text/html
// The DOCTYPE name or the root element is "html".
extern "C" fn html_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probe) = xml_probe_typefinder(tf)
        && probe.named("html")
    {
        suggest!(tf, Prob::MAXIMUM, c"text/html");
    }
}

// SVG - image/svg+xml
// The DOCTYPE name or the root element is "svg" -> 100; else a bare "<svg"
// anywhere in the first 1 KiB -> 80.
extern "C" fn svg_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek_at_most(1024)
        && let Some(probe) = xml_probe(data)
    {
        if probe.named("svg") {
            suggest!(tf, Prob::MAXIMUM, c"image/svg+xml");
        } else if data.windows(4).any(|w| w.starts_with(b"<svg")) {
            suggest!(tf, Prob::LIKELY, c"image/svg+xml");
        }
    }
}

// HLS - application/x-hls
// "#EXTM3U" at the start, then within the first 4 KiB a line starting with
// #EXT-X-TARGETDURATION, #EXT-X-STREAM-INF or #EXT-X-MEDIA.
extern "C" fn hls_type_find(tf: &mut TypeFind, _private: gpointer) {
    // Directives are line items; a playlist may end with CR LF or LF.
    if let Some(data) = tf.peek_at_most(4096)
        && data.starts_with(b"#EXTM3U")
        && data.split(|b| *b == b'\n').any(|line| {
            let line = line.strip_suffix(b"\r").unwrap_or(line);
            line.starts_with(b"#EXT-X-TARGETDURATION")
                || line.starts_with(b"#EXT-X-STREAM-INF")
                || line.starts_with(b"#EXT-X-MEDIA")
        })
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-hls");
    }
}

// SDS - audio/x-sds
// Magic: masked match F0 7E 0? 01 (mask FF FF 80 FF) @ 0.
extern "C" fn sds_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<4>(0) else { return };
    const MATCH: [u8; 4] = [0xF0, 0x7E, 0x00, 0x01];
    const MASK: [u8; 4] = [0xFF, 0xFF, 0x80, 0xFF];
    if (0..4).all(|x| data[x] & MASK[x] == MATCH[x]) {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-sds");
    }
}

/// Whether `data` is text: valid UTF-8 that holds no NUL. A malformed
/// sequence in the last 3 bytes is a character cut off by the window edge
/// and counts as text, because the window is an arbitrary probe size. A
/// NUL dismisses the data wherever it sits: it is complete UTF-8 of its
/// own, never a fragment the edge could have made.
fn looks_like_text(data: &[u8]) -> bool {
    if data.contains(&0) {
        return false;
    }
    match std::str::from_utf8(data) {
        Ok(_) => true,
        Err(e) => e.valid_up_to() + 3 >= data.len(),
    }
}

// UTF-8 plausibility of the data at `offset`: the probe starts at 32 KiB
// (probability 95) and each failure halves the probe, subtracting 10 from
// the probability, down to 128 bytes.
fn utf8_prob(tf: &mut TypeFind, offset: i64) -> Option<u32> {
    let (mut size, mut prob) = match tf.length() {
        Some(len) => {
            let avail = (len.get() as i64 - offset).max(0) as u32;
            if avail < 128 {
                return None;
            }
            let cap = avail.min(32 * 1024);
            let size = if cap.is_power_of_two() {
                cap
            } else {
                cap.next_power_of_two() / 2
            };
            (size, 95 - 10 * (15 - size.trailing_zeros()))
        }
        _ => (32 * 1024, 95),
    };
    loop {
        if let Some(data) = tf.peek_var(offset, size) {
            return looks_like_text(data).then_some(prob);
        }
        if size <= 128 || prob <= 10 {
            return None;
        }
        size /= 2;
        prob -= 10;
    }
}

// Valid UTF-32: length a multiple of 4 and every 32-bit code unit a valid
// Unicode scalar value (char::from_u32 rejects surrogates and values
// >= 0x110000).
fn valid_utf32(data: &[u8], big_endian: bool) -> bool {
    data.len().is_multiple_of(4)
        && data.as_chunks::<4>().0.iter().all(|c| {
            let v = if big_endian {
                u32::from_be_bytes(*c)
            } else {
                u32::from_le_bytes(*c)
            };
            char::from_u32(v).is_some()
        })
}

// Decode a UTF-16 byte sequence to a String; None for odd length or an
// invalid or lone surrogate code unit. `char::decode_utf16` does the strict
// validation.
fn utf16_to_string(data: &[u8], big_endian: bool) -> Option<String> {
    if !data.len().is_multiple_of(2) {
        return None;
    }
    let units = data.as_chunks::<2>().0.iter().map(|p| {
        if big_endian {
            u16::from_be_bytes(*p)
        } else {
            u16::from_le_bytes(*p)
        }
    });
    char::decode_utf16(units)
        .collect::<Result<String, _>>()
        .ok()
}

// Text - text/plain, text/utf-16, text/utf-32
// One factory: the caps are decided by a BOM, or by UTF-8 validity.
extern "C" fn text_type_find(tf: &mut TypeFind, _private: gpointer) {
    // Probe window for the BOM tests: powers of two, at most 256 KiB. A known
    // length gives the largest window that fits, in one peek; an unknown one
    // doubles until the peek stops working, then peeks the final size.
    let window = if let Some(len) = tf.length() {
        let cap = len.get().min(256 * 1024) as usize;
        if cap < 2 {
            None
        } else {
            let size = if cap.is_power_of_two() {
                cap
            } else {
                cap.next_power_of_two() / 2
            };
            tf.peek_var(0, size as u32)
        }
    } else if tf.peek_var(0, 2).is_some() {
        let mut size = 2;
        while size < 256 * 1024 && tf.peek_var(0, (size * 2) as u32).is_some() {
            size *= 2;
        }
        tf.peek_var(0, size as u32)
    } else {
        None
    };

    // Fewer than 2 bytes: no BOM.
    let data = window.unwrap_or(&[]);
    // UTF-32 first: its LE BOM starts with the UTF-16 LE BOM, so a UTF-32 LE
    // stream would otherwise be reported as UTF-16.
    let unicode = if data.starts_with(b"\xFF\xFE\x00\x00") && valid_utf32(data, false) {
        Some((c"text/utf-32", false))
    } else if data.starts_with(b"\x00\x00\xFE\xFF") && valid_utf32(data, true) {
        Some((c"text/utf-32", true))
    } else if data.starts_with(b"\xFF\xFE") && utf16_to_string(data, false).is_some() {
        Some((c"text/utf-16", false))
    } else if data.starts_with(b"\xFE\xFF") && utf16_to_string(data, true).is_some() {
        Some((c"text/utf-16", true))
    } else {
        None
    };
    if let Some((caps, big_endian)) = unicode {
        // A BOM is a strong hint but not proof of an encoding, so the
        // confidence stays at POSSIBLE (50). The big-endian order is the rarer
        // one, so its BOM carries the higher value.
        let (prob, endianness) = if big_endian {
            (50, bindings::G_BIG_ENDIAN)
        } else {
            (40, bindings::G_LITTLE_ENDIAN)
        };
        suggest!(tf, prob, caps, [(c"endianness", i32 : endianness as i32),]);
        return;
    }

    // Not BOM-prefixed: text/plain if the data is valid UTF-8. XML and SDP are
    // left to their own finders.
    if let Some(xml) = xml_probe_typefinder(tf)
        && xml.has_prolog
    {
        return;
    }
    if let Some(data) = tf.peek::<5>(0)
        && is_sdp_header(data)
    {
        return;
    }
    let Some(start_prob) = utf8_prob(tf, 0) else {
        return;
    };
    let Some(len) = tf.length() else {
        // Unknown length: POSSIBLE is the ceiling.
        suggest!(tf, start_prob.min(Prob::POSSIBLE.get()), c"text/plain");
        return;
    };
    if len.get() < 64 * 1024 {
        suggest!(tf, start_prob, c"text/plain");
        return;
    }
    let Some(mid_prob) = utf8_prob(tf, (len.get() / 2) as i64) else {
        return;
    };
    suggest!(tf, (start_prob + mid_prob) / 2, c"text/plain");
}

// Smooth Streaming Manifest - application/vnd.ms-sstr+xml
// Element matcher (strict) on "SmoothStreamingMedia"; the probe strips a UTF-8
// BOM itself.
extern "C" fn mss_manifest_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(XML_WINDOW) else {
        return;
    };
    // The probe expects UTF-8: decode a UTF-16 BOM first. A failed decode
    // falls back to the raw bytes, which the probe then rejects.
    let decoded = match data.get(..2) {
        Some(b"\xFF\xFE") => utf16_to_string(&data[2..], false).map(String::into_bytes),
        Some(b"\xFE\xFF") => utf16_to_string(&data[2..], true).map(String::into_bytes),
        _ => None,
    };
    let text: &[u8] = decoded.as_deref().unwrap_or(data);
    if let Some(probe) = xml_probe(text)
        && probe.has_prolog
        && probe.root_is("SmoothStreamingMedia")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/vnd.ms-sstr+xml");
    }
}

// URI list - text/uri-list
// The first line that is not a "#" comment has to be a "scheme://" URI, the
// scheme starting with a letter and holding only alphanumerics. Scanned
// within the first 4 KiB.
extern "C" fn uri_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(4096) else {
        return;
    };
    if let Some(line) = data.split(|b| *b == b'\n').find(|l| !l.starts_with(b"#")) {
        let mut parts = line.splitn(2, |b| *b == b':');
        if let (Some(scheme), Some(rest)) = (parts.next(), parts.next())
            && rest.starts_with(b"//")
            && scheme.first().is_some_and(u8::is_ascii_alphabetic)
            && scheme.iter().all(u8::is_ascii_alphanumeric)
        {
            suggest!(tf, Prob::LIKELY, c"text/uri-list");
        }
    }
}

// =============================================================================
// Legacy game video and audio
// =============================================================================

// Smacker - application/x-smk
// A valid signature with implausible frame dimensions still identifies the
// container, only less surely.
extern "C" fn smk_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<12>(0) else {
        return;
    };
    if data[..4] != *b"SMK2" && data[..4] != *b"SMK4" {
        return;
    }
    let width = u32::from_le_bytes([data[4], data[5], data[6], data[7]]);
    let height = u32::from_le_bytes([data[8], data[9], data[10], data[11]]);
    if width > 32768 || height > 32768 {
        suggest!(tf, Prob::MINIMUM, c"application/x-smk");
    } else {
        suggest!(tf, Prob::MAXIMUM, c"application/x-smk");
    }
}

// Sierra SOL - application/x-sol
// The version word is followed by the format name at offset 2.
extern "C" fn sol_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<8>(0) else {
        return;
    };
    let magic = u16::from_le_bytes([data[0], data[1]]);
    if !matches!(magic, 0x0B8D | 0x0C0D | 0x0C8D) {
        return;
    }
    if data[2..6] != *b"SOL\0" {
        return;
    }
    suggest!(tf, Prob::MAXIMUM, c"application/x-sol");
}

// Sierra VMD - application/x-vmd
// The header is 806 bytes: dimensions up front, sample rate at the very end.
// Dimensions are only checked when the sample rate does not settle it.
extern "C" fn vmd_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<806>(0) else {
        return;
    };
    if u16::from_le_bytes([data[0], data[1]]) != 0x032E {
        return;
    }
    let width = u16::from_le_bytes([data[12], data[13]]);
    let height = u16::from_le_bytes([data[14], data[15]]);
    let rate = u16::from_le_bytes([data[804], data[805]]);
    let dims_ok = (1..=2048).contains(&width) && (1..=2048).contains(&height);
    if !dims_ok && rate != 22050 {
        return;
    }
    suggest!(tf, Prob::POSSIBLE, c"application/x-vmd");
}

// American Laser Games MM - application/x-mm
extern "C" fn mm_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<30>(0) else {
        return;
    };
    if data[..2] != [0, 0] {
        return;
    }
    // Header length selects where the codec type is read from.
    let header_len = u32le(&data[2..]) as usize;
    if !matches!(header_len, 22 | 24) {
        return;
    }
    let fps = u16le(&data[8..]);
    let width = u16le(&data[12..]);
    let height = u16le(&data[14..]);
    if !(1..=60).contains(&fps) || !(1..=2048).contains(&width) || !(1..=2048).contains(&height) {
        return;
    }
    if !(1..=0x31).contains(&u16le(&data[header_len..])) {
        return;
    }
    suggest!(tf, Prob::POSSIBLE, c"application/x-mm");
}

// Wing Commander III movie - application/x-wc3movie
extern "C" fn wc3movie_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<12>(0) else {
        return;
    };
    if data[..4] == *b"FORM" && data[8..12] == *b"MOVE" {
        suggest!(tf, Prob::MAXIMUM, c"application/x-wc3movie");
    }
}

// Westwood Studios VQA - application/x-wsvqa
extern "C" fn wsvqa_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<12>(0) else {
        return;
    };
    if data[..4] == *b"FORM" && data[8..12] == *b"WVQA" {
        suggest!(tf, Prob::MAXIMUM, c"application/x-wsvqa");
    }
}

// Westwood Studios audio - application/x-wsaud
extern "C" fn wsaud_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<20>(0) else {
        return;
    };
    let rate = u16::from_le_bytes([data[0], data[1]]);
    if !(8000..=48000).contains(&rate) || data[10] & 0xFC != 0 {
        return;
    }
    if data[11] != 99 && data[11] != 1 {
        return;
    }
    if u32::from_le_bytes([data[16], data[17], data[18], data[19]]) != 0x0000_DEAF {
        return;
    }
    suggest!(tf, Prob::POSSIBLE, c"application/x-wsaud");
}

// Vivo - video/vivo
// The header length is validated but not used for positioning; the version
// marker follows the header field and any CRLF pairs after it.
extern "C" fn vivo_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek::<1024>(0) else {
        return;
    };
    if data[0] != 0 {
        return;
    }
    let mut pos = if data[1] & 0x80 != 0 {
        if data[2] & 0x80 != 0 {
            return;
        }
        let header_len = ((data[1] as usize & 0x7F) << 7) + data[2] as usize;
        if header_len > 2048 {
            return;
        }
        3
    } else {
        2
    };
    while pos < 1008 && data[pos] == b'\r' && data[pos + 1] == b'\n' {
        pos += 2;
    }
    if data[pos..].starts_with(b"Version:Vivo/") {
        suggest!(tf, Prob::MAXIMUM, c"video/vivo");
    }
}

// id Cinematic - video/x-idcin
// The header is followed by a 64 KiB palette; a 256-colour palette is
// extended by a 768-byte ramp, which shifts the frame size field.
extern "C" fn idcin_type_find(tf: &mut TypeFind, _private: gpointer) {
    const PALETTE: usize = 20 + 64 * 1024;
    let Some(data) = tf.peek::<{ PALETTE + 4 }>(0) else {
        return;
    };
    let width = u32le(&data[..]);
    let height = u32le(&data[4..]);
    let rate = u32le(&data[8..]);
    if !(1..=1024).contains(&width) || !(1..=1024).contains(&height) {
        return;
    }
    if rate != 0 && !(8000..=48000).contains(&rate) {
        return;
    }
    // The two codec fields are unused when there is no audio track.
    for offset in [12, 16] {
        let codec = u32le(&data[offset..]);
        if codec > 2 || (rate != 0 && codec == 0) {
            return;
        }
    }
    let mut pos = PALETTE;
    if u32le(&data[pos..]) == 1 {
        pos += 768;
    }
    let Some(data) = tf.peek_var(0, (pos + 12) as u32) else {
        return;
    };
    if u32le(&data[pos + 8..]) == width * height {
        suggest!(tf, Prob::POSSIBLE, c"video/x-idcin");
    }
}

// =============================================================================
// AAC interchange formats
// =============================================================================

extern "C" fn aac_type_find(tf: &mut TypeFind, _private: gpointer) {
    match aac_probe(tf) {
        Some(AacFraming::Adif) => {
            suggest!(tf, 80, c"audio/mpeg", [
                (c"framed", bool: false),
                (c"mpegversion", i32: 4),
                (c"stream-format", str: c"adif"),
            ]);
        }
        Some(AacFraming::Loas { probability }) => {
            suggest!(tf, probability, c"audio/mpeg", [
                (c"framed", bool: false),
                (c"mpegversion", i32: 4),
                (c"stream-format", str: c"loas"),
            ]);
        }
        Some(AacFraming::Adts {
            probability,
            mpegversion,
            layout,
            level,
            profile,
        }) => {
            // A level that matches nothing is left out rather than guessed,
            // and base-profile is an MPEG-4 field.
            let mpeg4 = mpegversion == 4;
            match (layout, level) {
                (Some((channels, rate)), Some(level)) if mpeg4 => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"level", str: level),
                        (c"base-profile", str: profile),
                        (c"profile", str: profile),
                        (c"channels", i32: channels),
                        (c"rate", i32: rate),
                    ]);
                }
                (Some((channels, rate)), Some(level)) => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"level", str: level),
                        (c"profile", str: profile),
                        (c"channels", i32: channels),
                        (c"rate", i32: rate),
                    ]);
                }
                (Some((channels, rate)), None) if mpeg4 => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"base-profile", str: profile),
                        (c"profile", str: profile),
                        (c"channels", i32: channels),
                        (c"rate", i32: rate),
                    ]);
                }
                (Some((channels, rate)), None) => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"profile", str: profile),
                        (c"channels", i32: channels),
                        (c"rate", i32: rate),
                    ]);
                }
                // A layout is what a level is computed from, so there is
                // nothing to name without one.
                (None, _) if mpeg4 => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"base-profile", str: profile),
                        (c"profile", str: profile),
                    ]);
                }
                (None, _) => {
                    suggest!(tf, probability, c"audio/mpeg", [
                        (c"framed", bool: false),
                        (c"mpegversion", i32: mpegversion),
                        (c"stream-format", str: c"adts"),
                        (c"profile", str: profile),
                    ]);
                }
            }
        }
        None => {}
    }
}

// =============================================================================
// DTS
// =============================================================================

extern "C" fn dts_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(frame) = dts_probe(tf) else { return };
    // A channel index the format leaves unassigned says nothing about the
    // layout, so the field is left out rather than guessed.
    if frame.channels > 0 {
        suggest!(tf, frame.probability, c"audio/x-dts", [
            (c"rate", i32: frame.rate),
            (c"channels", i32: frame.channels),
            (c"depth", i32: frame.depth),
            (c"endianness", i32: frame.endianness),
            (c"framed", bool: false),
        ]);
    } else {
        suggest!(tf, frame.probability, c"audio/x-dts", [
            (c"rate", i32: frame.rate),
            (c"depth", i32: frame.depth),
            (c"endianness", i32: frame.endianness),
            (c"framed", bool: false),
        ]);
    }
}

// =============================================================================
// AC-3 and E-AC-3
// =============================================================================

extern "C" fn ac3_type_find(tf: &mut TypeFind, _private: gpointer) {
    match ac3_probe(tf) {
        Some((probability, Ac3Stream::Ac3)) => suggest!(tf, probability, c"audio/x-ac3"),
        Some((probability, Ac3Stream::EAc3)) => suggest!(tf, probability, c"audio/x-eac3"),
        None => {}
    }
}

// =============================================================================
// MPEG-1 audio layers
// =============================================================================

extern "C" fn mp3_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(stream) = mp3_probe(tf) else { return };
    suggest!(tf, stream.probability, c"audio/mpeg", [
        (c"mpegversion", i32: 1),
        (c"layer", i32: stream.layer),
        (c"parsed", bool: false),
    ]);
}

// =============================================================================
// SBC
// =============================================================================

extern "C" fn sbc_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(frame) = sbc_probe(tf) else { return };
    // The framing is all a bare SBC stream has to offer, so the confidence
    // stops at possible. The marginal rank says the same thing.
    suggest!(tf, Prob::POSSIBLE, c"audio/x-sbc", [
        (c"rate", i32: frame.rate as i32),
        (c"channels", i32: frame.channels as i32),
        (c"parsed", bool: false),
    ]);
}

// =============================================================================
// WavPack
// =============================================================================

extern "C" fn wavpack_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(stream) = wavpack_probe(tf) else {
        return;
    };
    // `framed` belongs to the parser, which sets it once it has split the stream
    // into blocks. A stream as found is a bare run of them.
    if stream.correction {
        suggest!(tf, stream.probability, c"audio/x-wavpack-correction", [
            (c"framed", bool: false),
        ]);
    } else {
        suggest!(tf, stream.probability, c"audio/x-wavpack", [
            (c"framed", bool: false),
        ]);
    }
}

// =============================================================================
// AC-4
// =============================================================================

extern "C" fn ac4_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(probability) = ac4_probe(tf) else {
        return;
    };
    suggest!(tf, probability, c"audio/x-ac4", [
        (c"stream-format", str: c"syncframe"),
    ]);
}

// =============================================================================
// Audible AA
// =============================================================================

extern "C" fn aa_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(probability) = aa_probe(tf) else {
        return;
    };
    suggest!(tf, probability, c"audio/x-audible");
}

extern "C" fn mod_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some((probability, family)) = mod_probe(tf) else {
        return;
    };
    suggest!(tf, probability, c"audio/x-mod", [
        (c"type", str : family),
    ]);
}

// =============================================================================
// Images
// =============================================================================

// Netpbm whitespace: what the ANSI C definition of white space covers.
fn pnm_whitespace(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\r' | b'\n' | b'\x0b' | b'\x0c')
}

// The two decimal dimensions of a netpbm header, whitespace and comments
// skipped. Called with the header past the magic. The dimensions are read as
// the 32-bit signed integers the caps carry, so an out-of-range header is not
// a dimension at all.
fn pnm_size(data: &[u8]) -> Option<(i32, i32)> {
    let mut dims = [0i32; 2];
    let mut pos = 0;
    for dim in &mut dims {
        loop {
            match data.get(pos) {
                Some(b) if pnm_whitespace(*b) => pos += 1,
                Some(b'#') => {
                    pos += 1 + data[pos + 1..]
                        .iter()
                        .position(|&b| b == b'\n' || b == b'\r')?;
                }
                Some(_) => break,
                None => return None,
            }
        }
        let start = pos;
        while data.get(pos).is_some_and(u8::is_ascii_digit) {
            pos += 1;
        }
        *dim = std::str::from_utf8(&data[start..pos]).ok()?.parse().ok()?;
    }
    (dims[0] > 0 && dims[1] > 0).then_some((dims[0], dims[1]))
}

// The media type a netpbm magic digit names: P1 and P4 are bitmaps, P2 and P5
// graymaps, P3 and P6 pixmaps, P7 the arbitrary map.
fn pnm_caps(digit: u8) -> Option<&'static CStr> {
    match digit {
        b'1' | b'4' => Some(c"image/x-portable-bitmap"),
        b'2' | b'5' => Some(c"image/x-portable-graymap"),
        b'3' | b'6' => Some(c"image/x-portable-pixmap"),
        b'7' => Some(c"image/x-portable-anymap"),
        _ => None,
    }
}

// Netpbm - image/x-portable-bitmap, -graymap, -pixmap, -anymap
// Magic, whitespace, then the fields; "#" comments are allowed wherever the
// header has whitespace. Width and height are reported when they parse.
extern "C" fn pnm_type_find(tf: &mut TypeFind, _private: gpointer) {
    // The header is a few dozen bytes; the slack in the peek is for comments.
    if let Some(data) = tf.peek_between(3, 256)
        && data[0] == b'P'
        && let Some(caps) = pnm_caps(data[1])
        && pnm_whitespace(data[2])
    {
        // A P7 header names its fields instead of giving them positionally,
        // so there are no dimensions to read from it.
        let dims = if data[1] == b'7' {
            None
        } else {
            pnm_size(&data[2..])
        };
        if let Some((width, height)) = dims {
            suggest!(tf, Prob::MAXIMUM, caps, [
                (c"width", i32 : width),
                (c"height", i32 : height),
            ]);
        } else {
            suggest!(tf, Prob::LIKELY, caps);
        }
    }
}

// WBMP multi-byte integer: seven bits per octet, most significant first, the
// top bit set on every octet but the last. The encoding has to be minimal.
fn wbmp_int(data: &[u8]) -> Option<(u32, usize)> {
    if data.first() == Some(&0x80) {
        return None;
    }
    let mut value: u64 = 0;
    for (i, &b) in data.iter().take(5).enumerate() {
        value = (value << 7) | (b & 0x7F) as u64;
        if b & 0x80 == 0 {
            return u32::try_from(value).ok().map(|v| (v, i + 1));
        }
    }
    None
}

// WBMP type 0 - image/vnd.wap.wbmp
// Type 0, no extension header, multi-byte width and height, then height rows
// of ceil(width/8) bytes. The length has to come out exact: the format has no
// magic, so an exact bitmap is what separates a WBMP from a binary file that
// happens to start with two zero octets.
extern "C" fn wbmp_type_find(tf: &mut TypeFind, _private: gpointer) {
    // The type and fix-header octets are both zero, then up to five octets
    // per dimension.
    if let Some(length) = tf.length()
        && let Some(data) = tf.peek_between(2, 12)
        && data[..2] == [0, 0]
        && let Some((width, n)) = wbmp_int(&data[2..])
        && width > 0
        && let Some((height, m)) = wbmp_int(&data[2 + n..])
        && height > 0
    {
        let bitmap = height as u64 * (width as u64).div_ceil(8);
        if (2 + n + m) as u64 + bitmap == length.get() {
            suggest!(tf, Prob::POSSIBLE.get() - 10, c"image/vnd.wap.wbmp");
        }
    }
}

// Windows icon - image/x-icon
// ICONDIR: reserved, type, image count, then 16-byte directory entries. The
// first entry has to describe image data that is inside the file.
extern "C" fn windows_icon_type_find(tf: &mut TypeFind, _private: gpointer) {
    const ICON: u16 = 1;
    const CURSOR: u16 = 2;

    let Some(length) = tf.length() else {
        return;
    };
    // Header and first directory entry.
    let Some(data) = tf.peek::<22>(0) else {
        return;
    };
    if data[..2] != [0, 0] {
        return;
    }
    // The entry: width, height, colour count, reserved octet, planes, bit
    // count, size and image offset.
    let entry = &data[6..];
    let kind = u16le(&data[2..]);
    let count = u16le(&data[4..]);
    let planes = u16le(&entry[4..]);
    let size = u32le(&entry[8..]);
    let offset = u32le(&entry[12..]);
    let length = length.get();
    let (size, offset) = (u64::from(size), u64::from(offset));
    if matches!(kind, ICON | CURSOR)
        && count > 0
        && entry[3] == 0
        // Icons are single-plane; a cursor carries its hotspot here instead.
        && (kind == CURSOR || planes <= 1)
        // The entry has to point at image data inside the file.
        && size > 0
        && size < length
        && offset < length
        && size + offset <= length
    {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"image/x-icon");
    }
}

// The four u16 values 16 bytes from the end of an Elite picture: each one a
// resolution, so 0 to 2.
fn degas_tail(tf: &mut TypeFind, length: u64) -> bool {
    let Some(tail) = tf.peek_var(length as i64 - 16, 8) else {
        return false;
    };
    tail.as_chunks::<2>()
        .0
        .iter()
        .all(|b| u16::from_be_bytes(*b) <= 2)
}

// Atari DEGAS picture - image/x-degas
// No magic: the plain pictures are 32034 bytes, the Elite pictures 32066, and
// compressed Elite pictures are smaller still. The resolution, 0 to 2 for low,
// medium and high, carries the compressed flag in its top bit.
extern "C" fn degas_type_find(tf: &mut TypeFind, _private: gpointer) {
    const PI: u64 = 32034;
    const ELITE: u64 = 32066;
    if let Some(length) = tf.length().map(NonZeroU64::get)
        && let Some(data) = tf.peek::<4>(0)
        && let resolution = u16::from_be_bytes([data[0], data[1]])
        && match length {
            PI => resolution <= 2,
            ELITE => resolution <= 2 && degas_tail(tf, length),
            66..ELITE => {
                resolution & 0x8000 != 0 && resolution & 0x7FFF <= 2 && degas_tail(tf, length)
            }
            _ => false,
        }
    {
        suggest!(tf, Prob::POSSIBLE.get() + 5, c"image/x-degas");
    }
}

// Apple iTunes artwork - application/itc
// Each stage that holds raises the confidence; a file that stops short of a
// stage is still reported for the stages it covers.
extern "C" fn itc_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_between(8, 292) else {
        return;
    };
    if data[..8] != *b"\0\0\x01\x1Citch" {
        return;
    }
    let preamble = data.len() >= 20 && data[8..20] == *b"\0\0\0\x02\0\0\0\x02\0\0\0\x02";
    let artwork = preamble && data.len() >= 28 && data[20..28] == *b"\0\0\0\0artw";
    let padding = artwork && data.len() >= 284 && data[28..284].iter().all(|&b| b == 0);
    let item = padding && data.len() >= 292 && data[288..292] == *b"item";
    let prob = if item {
        Prob::MAXIMUM
    } else if padding {
        Prob::NEARLY_CERTAIN
    } else if artwork {
        Prob::LIKELY
    } else if preamble {
        Prob::POSSIBLE
    } else {
        Prob::MINIMUM
    };
    suggest!(tf, prob, c"application/itc");
}

// =============================================================================
// MacCaption captions, MIME multipart
// =============================================================================

/// The major version digit of a MacCaption caption file, which is what the caps
/// carry. The header is a fixed tag followed by `V<major>.<minor>`.
fn mcc_probe(tf: &mut TypeFind) -> Option<i32> {
    let head = tf.peek::<31>(0)?;
    let Some((tag, [major, b'.', minor])) = head.split_last_chunk::<3>() else {
        return None;
    };
    if !tag.starts_with(b"File Format=MacCaption_MCC V")
        || !major.is_ascii_digit()
        || !minor.is_ascii_digit()
    {
        return None;
    }
    Some(i32::from(*major - b'0'))
}

/// The delimiter is looked for in the first few bytes and the header area in the
/// first 256. RFC 2046 5.1.1 gives every body part a header area, and in
/// practice the first part names its content there. A preamble ahead of the
/// delimiter is legal, so the narrow window is a heuristic rather than a rule.
const MULTIPART_DELIMITER_MAX: usize = 16;
const MULTIPART_HEADER_MAX: usize = 256;
const MULTIPART_CONTENT_TYPE: &[u8; 13] = b"content-type:";

fn multipart_probe(tf: &mut TypeFind) -> bool {
    let Some(delimiter) = tf.peek::<MULTIPART_DELIMITER_MAX>(0) else {
        return false;
    };
    // Linear whitespace, because a stream handed over by an HTTP client can
    // arrive with the line break still in front.
    let lead = delimiter[..MULTIPART_DELIMITER_MAX - 2]
        .iter()
        .position(|b| !b.is_ascii_whitespace())
        .unwrap_or(MULTIPART_DELIMITER_MAX - 2);
    if delimiter[lead..lead + 2] != *b"--" {
        return false;
    }

    let Some(head) = tf.peek::<MULTIPART_HEADER_MAX>(0) else {
        return false;
    };
    // ASCII only: a high-bit byte ahead of the header line means this is not a
    // text part, so the search stops rather than runs on into a binary body.
    for window in head[..MULTIPART_HEADER_MAX - 1].windows(MULTIPART_CONTENT_TYPE.len() + 1) {
        if !window[0].is_ascii() {
            return false;
        }
        if window[0] == b'\n' && window[1..].eq_ignore_ascii_case(MULTIPART_CONTENT_TYPE) {
            return true;
        }
    }
    false
}

extern "C" fn mcc_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(version) = mcc_probe(tf) else {
        return;
    };
    suggest!(
        tf,
        Prob::MAXIMUM,
        c"application/x-mcc",
        [(c"version", i32: version),]
    );
}

extern "C" fn multipart_type_find(tf: &mut TypeFind, _private: gpointer) {
    if multipart_probe(tf) {
        suggest!(tf, Prob::MAXIMUM, c"multipart/x-mixed-replace");
    }
}

// =============================================================================
// Containers
// =============================================================================

extern "C" fn qt_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some((probability, variant)) = qt_probe(tf) else {
        return;
    };
    if let Some(variant) = variant {
        suggest!(tf, probability, c"video/quicktime", [
            (c"variant", str : variant),
        ]);
    } else {
        suggest!(tf, probability, c"video/quicktime");
    }
}

extern "C" fn qtif_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = qtif_probe(tf) {
        suggest!(tf, probability, c"image/x-quicktime");
    }
}

extern "C" fn q3gp_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(profile) = q3gp_probe(tf) {
        suggest!(tf, Prob::MAXIMUM, c"application/x-3gp", [
            (c"profile", str : profile),
        ]);
    }
}

// =============================================================================
// H.26x elementary video streams
// =============================================================================

extern "C" fn h263_video_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = h263_probe(tf) {
        suggest!(tf, probability, c"video/x-h263", [
            (c"variant", str : c"itu"),
        ]);
    }
}

extern "C" fn h264_video_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = h264_probe(tf) {
        suggest!(tf, probability, c"video/x-h264", [
            (c"stream-format", str : c"byte-stream"),
        ]);
    }
}

extern "C" fn h265_video_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = h265_probe(tf) {
        suggest!(tf, probability, c"video/x-h265", [
            (c"stream-format", str : c"byte-stream"),
        ]);
    }
}

extern "C" fn h266_video_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = h266_probe(tf) {
        suggest!(tf, probability, c"video/x-h266", [
            (c"stream-format", str : c"byte-stream"),
        ]);
    }
}

// Matroska and WebM - video/x-matroska
// EBML header @ 0; a DocType and the first tracks set the media type.
extern "C" fn matroska_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some((caps, probability)) = matroska_probe(tf) {
        suggest!(tf, probability, caps);
    }
}

// AV1 - video/x-av1
// A parseable OBU header at offset 0, bare or length-prefixed.
extern "C" fn av1_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some((stream_format, probability)) = av1_probe(tf) {
        suggest!(tf, probability, c"video/x-av1", [
            (c"stream-format", str : stream_format),
            (c"alignment", str : c"none"),
        ]);
    }
}

// =============================================================================
// MPEG program streams, transport streams and video elementary streams
// =============================================================================

extern "C" fn mpeg_sys_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some((probability, mpegversion)) = mpeg_sys_probe(tf) {
        suggest!(tf, probability, c"video/mpeg", [
            (c"systemstream", bool: true),
            (c"mpegversion", i32: mpegversion),
        ]);
    }
}

extern "C" fn mpeg_ts_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some((probability, packetsize)) = mpeg_ts_probe(tf) {
        suggest!(tf, probability, c"video/mpegts", [
            (c"systemstream", bool: true),
            (c"packetsize", i32: packetsize),
        ]);
    }
}

extern "C" fn mpeg_video_stream_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = mpeg_video_probe(tf) {
        suggest!(tf, probability, c"video/mpeg", [
            (c"systemstream", bool: false),
            (c"mpegversion", i32: 1),
            (c"parsed", bool: false),
        ]);
    }
}

extern "C" fn mpeg4_video_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(probability) = mpeg4_video_probe(tf) {
        suggest!(tf, probability, c"video/mpeg", [
            (c"systemstream", bool: false),
            (c"mpegversion", i32: 4),
            (c"parsed", bool: false),
        ]);
    }
}

// =============================================================================
// Registration table and entry point
// =============================================================================

/// Registration table: one entry per typefinder. See the `typefinder!` macro
/// for the signatures and what each argument means.
static FEATURES: &[Feature] = &[
    // Trivial finders - magic bytes only
    start_with!(
        c"application/x-shockwave-flash",
        SECONDARY,
        c"swf,swfl",
        [b"FWS", b"CWS"],
        Prob::MAXIMUM
    ),
    typefinder!(
        c"image/tiff",
        PRIMARY,
        tiff_type_find,
        c"tif,tiff",
        c"image/tiff, endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }"
    ),
    start_with!(c"video/x-flv", SECONDARY, c"flv", [b"FLV"], Prob::MAXIMUM),
    start_with!(c"image/gif", PRIMARY, c"gif", [b"GIF8"], Prob::MAXIMUM),
    start_with!(c"video/x-dirac", PRIMARY, none, [b"BBCD", b"KW-DIRAC"], Prob::MAXIMUM),
    start_with!(c"video/x-theora", PRIMARY, none, [b"\x80theora"], Prob::MAXIMUM),
    start_with!(c"application/x-gzip", SECONDARY, c"gz", [b"\x1f\x8b"], Prob::LIKELY),
    start_with!(c"application/zip", SECONDARY, c"zip", [b"PK\x03\x04"], Prob::LIKELY),
    typefinder!(
        c"application/x-id3v2",
        PRIMARY + 103,
        id3v2_type_find,
        c"mp3,mp2,mp1,mpga,ogg,flac,tta",
        c"application/x-id3"
    ),
    typefinder!(c"audio/x-m4a", PRIMARY, m4a_type_find, c"m4a"),
    typefinder!(c"audio/mobile-xmf", PRIMARY, mxmf_type_find, c"mxmf"),
    typefinder!(c"application/x-ar", SECONDARY, ar_type_find, c"a"),
    start_with!(c"video/x-ms-asf", SECONDARY, c"asf,wm,wma,wmv", [&ASF_HEADER_GUID], Prob::MAXIMUM),
    typefinder!(c"application/x-mmsh", SECONDARY, mmsh_type_find),
    typefinder!(c"text/x-cmml", PRIMARY, cmml_type_find),
    // Note: typefindfunctions_subtitles registered below with no default caps.
    // Complex finders - structure validation and chunk/marker walking
    typefinder!(c"image/bmp", PRIMARY, bmp_type_find, c"bmp"),
    typefinder!(c"image/png", PRIMARY + 14, png_type_find, c"png"),
    typefinder!(c"image/x-exr", PRIMARY, exr_type_find, c"exr"),
    // JP2 can also detect MJ2 - register as both
    typefinder!(c"image/jp2", PRIMARY, jp2_type_find, c"jp2"),
    typefinder!(c"video/mj2", PRIMARY, jp2_type_find, c"mj2"),
    typefinder!(c"image/jpeg", PRIMARY + 15, jpeg_type_find, c"jpg,jpe,jpeg"),
    // Additional startswith finders
    start_with!(c"audio/midi", PRIMARY, c"mid,midi", [b"MThd"], Prob::MAXIMUM),
    start_with!(c"audio/x-ttafile", PRIMARY, c"tta", [b"TTA"], Prob::MAXIMUM),
    typefinder!(
        c"application/x-apetag",
        PRIMARY + 102,
        apetag_type_find,
        c"mp3,ape,mpc,wv"
    ),
    start_with!(
        c"application/x-ape",
        SECONDARY,
        c"ape",
        [b"MAC "],
        Prob::from_raw(90)
    ),
    start_with!(c"audio/x-au", MARGINAL, c"au,snd", [b".snd", b"dns."], Prob::MAXIMUM),
    typefinder!(c"audio/x-shorten", SECONDARY, shn_type_find, c"shn"),
    start_with!(c"audio/x-paris", SECONDARY, c"paf", [b" paf", b"fap "], Prob::MAXIMUM),
    start_with!(c"audio/x-celt", PRIMARY, none, [b"CELT    "], Prob::MAXIMUM),
    typefinder!(c"audio/x-svx", SECONDARY, svx_type_find, c"iff,svx"),
    start_with!(c"audio/iLBC-sh", PRIMARY, c"ilbc", [b"#!iLBC30", b"#!iLBC20"], Prob::LIKELY),
    typefinder!(c"audio/x-dsf", PRIMARY, dsf_type_find),
    typefinder!(c"audio/x-aiff", SECONDARY, aiff_type_find, c"aiff,aif,aifc"),
    typefinder!(c"audio/x-brstm", SECONDARY, brstm_type_find, c"brstm"),
    typefinder!(c"audio/x-bfstm", SECONDARY, bfstm_type_find, c"bfstm,bcstm"),
    start_with!(c"audio/x-tap-tap", PRIMARY, c"tap", [b"C64-TAPE-RAW", b"C16-TAPE-RAW"], Prob::MAXIMUM),
    start_with!(c"audio/x-amr-nb-sh", PRIMARY, c"amr", [b"#!AMR"], Prob::LIKELY),
    start_with!(c"audio/x-amr-wb-sh", PRIMARY, c"amr", [b"#!AMR-WB"], Prob::MAXIMUM),
    typefinder!(c"video/x-vcd", PRIMARY, vcd_type_find, c"dat"),
    start_with!(c"audio/x-imelody", PRIMARY, c"imy,ime,imelody", [b"BEGIN:IMELODY"], Prob::MAXIMUM),
    // Ogg Skeleton - no extensions; custom caps with parsed=false
    typefinder!(
        c"application/x-ogg-skeleton",
        PRIMARY,
        oggskel_type_find,
        none,
        c"application/x-ogg-skeleton, parsed=(boolean)FALSE"
    ),
    typefinder!(c"application/x-yuv4mpeg", PRIMARY, y4m_type_find, c"y4m"),
    start_with!(c"application/x-scc", PRIMARY, c"scc", [b"Scenarist_SCC V1.0"], Prob::MAXIMUM),
    start_with!(c"application/vnd.rn-realmedia", SECONDARY, c"ra,ram,rm,rmvb", [b".RMF"], Prob::MAXIMUM),
    start_with!(c"application/x-pn-realaudio", SECONDARY, c"ra,ram,rm,rmvb", [b".ra\xfd"], Prob::MAXIMUM),
    start_with!(c"audio/x-nist", SECONDARY, c"nist", [b"NIST"], Prob::MAXIMUM),
    start_with!(c"audio/x-voc", SECONDARY, c"voc", [b"Creative"], Prob::MAXIMUM),
    start_with!(c"audio/x-rf64", PRIMARY, c"rf64", [b"RF64"], Prob::MAXIMUM),
    start_with!(c"audio/x-sid", MARGINAL, c"sid", [b"PSID"], Prob::MAXIMUM),
    start_with!(c"image/x-xcf", SECONDARY, c"xcf", [b"gimp xcf"], Prob::MAXIMUM),
    start_with!(c"video/x-mng", SECONDARY, c"mng", [b"\x8aMNG\r\n\x1a\n"], Prob::MAXIMUM),
    start_with!(c"video/x-mve", SECONDARY, c"mve", [b"Interplay MVE File\x1a\x00\x1a\x00\x00\x01\x33\x11"], Prob::MAXIMUM),
    typefinder!(c"video/x-pva", SECONDARY, pva_type_find, c"pva"),
    typefinder!(c"video/x-film-cpk", SECONDARY, film_cpk_type_find, c"cpk"),
    typefinder!(
        c"application/x-id3v1",
        PRIMARY + 101,
        id3v1_type_find,
        c"mp3,mp2,mp1,mpga,ogg,flac,tta",
        c"application/x-id3"
    ),
    start_with!(c"image/x-jng", SECONDARY, c"jng", [b"\x8bJNG\r\n\x1a\n"], Prob::MAXIMUM),
    start_with!(c"image/x-sun-raster", SECONDARY, c"ras", [b"\x59\xa6\x6a\x95"], Prob::MAXIMUM),
    typefinder!(c"application/x-tar", SECONDARY, tar_type_find, c"tar"),
    start_with!(c"application/x-bzip", SECONDARY, c"bz2", [b"BZh"], Prob::LIKELY),
    start_with!(c"application/x-compress", SECONDARY, c"Z", [b"\x1f\x9d"], Prob::LIKELY),
    start_with!(c"application/x-executable", MARGINAL, none, [b"\x7fELF"], Prob::MAXIMUM),
    start_with!(c"audio/x-spc", SECONDARY, c"spc", [b"SNES-SPC700 Sound File Data"], Prob::MAXIMUM),
    start_with!(c"audio/x-caf", SECONDARY, c"caf", [b"caff\x00\x01"], Prob::MAXIMUM),
    start_with!(c"application/x-rar", SECONDARY, c"rar", [b"Rar!"], Prob::LIKELY),
    start_with!(c"audio/x-nsf", SECONDARY, c"nsf", [b"NESM\x1a"], Prob::MAXIMUM),
    start_with!(c"audio/x-gym", SECONDARY, c"gym", [b"GYMX"], Prob::MAXIMUM),
    start_with!(c"audio/x-ay", SECONDARY, c"ay", [b"ZXAYEMUL"], Prob::MAXIMUM),
    start_with!(c"audio/x-gbs", SECONDARY, c"gbs", [b"GBS\x01"], Prob::MAXIMUM),
    start_with!(c"audio/x-vgm", SECONDARY, c"vgm", [b"Vgm "], Prob::MAXIMUM),
    start_with!(c"audio/x-w64", SECONDARY, c"w64", [b"riff"], Prob::MAXIMUM),
    start_with!(c"audio/x-sap", SECONDARY, c"sap", [b"SAP\r\nAUTHOR "], Prob::MAXIMUM),
    start_with!(c"video/x-ivf", SECONDARY, c"ivf", [b"DKIF"], Prob::NEARLY_CERTAIN),
    start_with!(c"audio/x-kss", SECONDARY, c"kss", [b"KSSX\x00"], Prob::MAXIMUM),
    start_with!(c"application/pdf", SECONDARY, c"pdf", [b"%PDF-"], Prob::LIKELY),
    start_with!(c"application/msword", SECONDARY, c"doc", [b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"], Prob::LIKELY),
    typefinder!(c"video/x-4xm", PRIMARY, fourxm_type_find),
    typefinder!(
        c"application/postscript",
        SECONDARY,
        postscript_type_find,
        c"ps"
    ),
    // RIFF variants
    typefinder!(c"video/x-msvideo", PRIMARY, avi_type_find, c"avi,avf"),
    typefinder!(c"audio/qcelp", PRIMARY, qcp_type_find, c"qcp"),
    typefinder!(c"video/x-cdxa", PRIMARY, cdxa_type_find, c"dat"),
    typefinder!(c"audio/riff-midi", PRIMARY, riff_mid_type_find, c"mid,midi"),
    typefinder!(c"audio/x-wav", PRIMARY, wav_type_find, c"wav"),
    typefinder!(c"image/webp", PRIMARY, webp_type_find, c"webp"),
    typefinder!(c"audio/x-xwma", PRIMARY, xwma_type_find),
    start_with!(c"application/octet-stream", SECONDARY, c"DS_Store", [b"\x00\x00\x00\x01Bud1"], Prob::LIKELY),
    // Registers after the DS_Store row of the same name, and replaces it
    // in the registry: the feature answers for both, as it does upstream.
    typefinder!(c"application/octet-stream", MARGINAL, dvdiso_type_find),
    start_with!(c"audio/x-tap-dmp", SECONDARY, c"dmp", [b"DC2N-TAP-RAW"], Prob::LIKELY),
    start_with!(c"video/x-avs", SECONDARY, none, [b"\x77\x57\x10\x00"], Prob::LIKELY),
    start_with!(c"image/vnd.adobe.photoshop", SECONDARY, c"psd", [b"8BPS\x00\x01\x00\x00\x00\x00"], Prob::LIKELY),
    start_with!(c"audio/x-xi", SECONDARY, c"xi", [b"Extended Instrument: "], Prob::MAXIMUM),
    start_with!(c"image/x-xpixmap", SECONDARY, c"xpm", [b"/* XPM */"], Prob::MAXIMUM),
    start_with!(c"image/x-xbitmap", SECONDARY, c"xbm", [b"/* XBM */"], Prob::MAXIMUM),
    start_with!(c"image/x-farbfeld", SECONDARY, c"ff", [b"farbfeld"], Prob::NEARLY_CERTAIN),
    start_with!(c"image/qoi", SECONDARY, c"qoi", [b"qoif"], Prob::NEARLY_CERTAIN),
    start_with!(c"image/vnd.ms-dds", SECONDARY, c"dds", [b"DDS "], Prob::NEARLY_CERTAIN),
    // OTIO - JSON-based with OTIO_SCHEMA key
    typefinder!(
        c"application/vnd.pixar.opentimelineio+json",
        SECONDARY,
        otio_type_find,
        c"otio"
    ),
    // FLIC/FLC - video/x-fli
    typefinder!(c"video/x-fli", MARGINAL, flx_type_find, c"flc,fli"),
    // JPEG 2000 Code Stream - image/x-jpc
    typefinder!(c"image/x-jpc", PRIMARY, jpc_type_find, c"jpc,j2k"),
    // Musepack - audio/x-musepack
    typefinder!(
        c"audio/x-musepack",
        PRIMARY,
        musepack_type_find,
        c"mpc,mpp,mp+",
        c"audio/x-musepack, streamversion=(int) { 7, 8 }"
    ),
    // AAC - audio/aac, in its ADTS, LOAS and ADIF framings
    typefinder!(
        c"audio/aac",
        SECONDARY,
        aac_type_find,
        c"aac,adts,adif,loas",
        c"audio/mpeg, mpegversion = (int) { 2, 4 }, framed = (bool) false"
    ),
    // DTS - audio/x-dts
    typefinder!(
        c"audio/x-dts",
        SECONDARY,
        dts_type_find,
        c"dts",
        c"audio/x-dts"
    ),
    // AC-3 - audio/x-ac3, which also suggests audio/x-eac3
    typefinder!(
        c"audio/x-ac3",
        PRIMARY,
        ac3_type_find,
        c"ac3,eac3",
        c"audio/x-ac3"
    ),
    // MP3 - audio/mpeg
    typefinder!(
        c"audio/mpeg",
        PRIMARY,
        mp3_type_find,
        c"mp3,mp2,mp1,mpga",
        c"audio/mpeg, mpegversion = (int) 1, layer = (int) [ 1, 3 ]"
    ),
    // SBC - audio/x-sbc
    typefinder!(c"audio/x-sbc", MARGINAL, sbc_type_find, c"sbc", c"audio/x-sbc"),
    // WavPack - audio/x-wavpack. One finder answers for both streams, since
    // telling them apart is what the probe does.
    typefinder!(
        c"audio/x-wavpack",
        SECONDARY,
        wavpack_type_find,
        c"wv,wvp",
        c"audio/x-wavpack, framed = (boolean) false"
    ),
    // WavPack correction - audio/x-wavpack-correction
    typefinder!(
        c"audio/x-wavpack-correction",
        SECONDARY,
        wavpack_type_find,
        c"wvc",
        c"audio/x-wavpack-correction, framed = (boolean) false"
    ),
    // AC-4 - audio/x-ac4
    typefinder!(
        c"audio/x-ac4",
        PRIMARY,
        ac4_type_find,
        c"ac4",
        c"audio/x-ac4, stream-format = (string) syncframe"
    ),
    // Audible AA - audio/audible. The registered name and the suggested caps
    // differ: the caps are what a match suggests, the name is the registry
    // feature name, which is also what gst-inspect prints for the factory.
    typefinder!(
        c"audio/audible",
        MARGINAL,
        aa_type_find,
        c"aa,aax",
        c"audio/x-audible"
    ),
    // GSM - audio/x-gsm. A bare GSM stream is a run of frames with no header and
    // no magic, so there is nothing to recognise from content and only the file
    // extension can answer.
    typefinder!(
        c"audio/x-gsm",
        PRIMARY,
        none,
        c"gsm",
        c"audio/x-gsm, rate=8000, channels=1"
    ),
    // Tracker modules - audio/x-mod. One media type for twenty eight tracker
    // families, which the `type` property on the suggestion tells apart.
    typefinder!(
        c"audio/x-mod",
        SECONDARY,
        mod_type_find,
        c"669,amf,ams,dbm,digi,dmf,dsm,gdm,far,imf,it,j2b,mdl,med,mod,mt2,mtm,okt,psm,ptm,sam,s3m,stm,stx,ult,umx,xm"
    ),
    // NUV (MythTVVideo/NuppelVideo) - video/x-nuv
    start_with!(c"video/x-nuv", SECONDARY, c"nuv", [b"MythTVVideo", b"NuppelVideo"], Prob::MAXIMUM),
    // DV - video/x-dv
    typefinder!(
        c"video/x-dv",
        SECONDARY,
        dv_type_find,
        c"dv,dif",
        c"video/x-dv, systemstream=(boolean)true"
    ),
    // Kate - caps (subtitle or application) are selected at runtime;
    // no file extensions are registered.
    typefinder!(c"subtitle/x-kate", MARGINAL, kate_type_find, none, none),
    // OGM video/audio/text - no extensions.
    start_with!(c"application/x-ogm-video", PRIMARY, none, [b"\x01video\x00\x00\x00"], Prob::MAXIMUM),
    start_with!(c"application/x-ogm-audio", PRIMARY, none, [b"\x01audio\x00\x00\x00"], Prob::MAXIMUM),
    start_with!(c"application/x-ogm-text", PRIMARY, none, [b"\x01text\x00\x00\x00\x00"], Prob::MAXIMUM),
    typefinder!(c"audio/x-flac", PRIMARY, flac_type_find, c"flac"),
    // SDP - application/sdp
    typefinder!(c"application/sdp", SECONDARY, sdp_type_find, c"sdp"),
    // EA - video/x-ea
    typefinder!(c"video/x-ea", SECONDARY, ea_type_find),
    // NSV - video/x-nsv
    typefinder!(c"video/x-nsv", MARGINAL, nsv_type_find, c"nsv"),
    // NUT - application/x-nut
    typefinder!(c"application/x-nut", MARGINAL, nut_type_find, c"nut"),
    // Interplay MVE. video/x-ipmovie names the codec, as gst-libav's map
    // and the demuxer do; upstream registered application/x-ipmovie.
    typefinder!(c"video/x-ipmovie", SECONDARY, ipmovie_type_find),
    // SMAF - application/vnd.smaf
    typefinder!(c"application/vnd.smaf", SECONDARY, mmf_type_find, c"mmf"),
    // IFF - application/x-iff
    typefinder!(c"application/x-iff", SECONDARY, iff_type_find),
    // GXF - application/gxf
    typefinder!(c"application/gxf", SECONDARY, gxf_type_find),
    // MS-DOS/PE executable - application/x-ms-dos-executable
    typefinder!(
        c"application/x-ms-dos-executable",
        SECONDARY,
        msdos_type_find,
        c"dll,exe,ocx,sys,scr,msstyles,cpl"
    ),
    // Speex - audio/x-speex
    typefinder!(c"audio/x-speex", PRIMARY, speex_type_find),
    // Vorbis - audio/x-vorbis
    typefinder!(c"audio/x-vorbis", PRIMARY, vorbis_type_find),
    // Ogg/Annodex - application/ogg
    typefinder!(
        c"application/ogg",
        PRIMARY,
        ogganx_type_find,
        c"ogg,oga,ogv,ogm,ogx,spx,anx,axa,axv",
        c"application/ogg;video/ogg;audio/ogg;application/kate"
    ),
    // IRCAM - audio/x-ircam
    typefinder!(c"audio/x-ircam", SECONDARY, ircam_type_find, c"sf"),
    // XML - application/xml
    typefinder!(c"application/xml", MARGINAL, xml_type_find, c"xml"),
    // DASH MPD - application/dash+xml
    typefinder!(
        c"application/dash+xml",
        PRIMARY,
        dash_mpd_type_find,
        c"mpd,MPD"
    ),
    // GES - application/xges
    typefinder!(c"application/xges", PRIMARY, xges_type_find, c"xges"),
    // FCPXML - application/vnd.apple-fcp+xml
    typefinder!(
        c"application/vnd.apple-fcp+xml",
        SECONDARY,
        fcpxml_type_find,
        c"fcpxml"
    ),
    // XMEML - application/vnd.apple-xmeml+xml
    typefinder!(
        c"application/vnd.apple-xmeml+xml",
        SECONDARY,
        xmeml_type_find,
        c"xmeml"
    ),
    // SMIL - application/smil
    typefinder!(c"application/smil", SECONDARY, smil_type_find, c"smil"),
    // TTML - application/ttml+xml
    typefinder!(
        c"application/ttml+xml",
        SECONDARY,
        ttml_xml_type_find,
        c"ttml+xml"
    ),
    // HTML - text/html
    typefinder!(c"text/html", SECONDARY, html_type_find, c"htm,html"),
    // SVG - image/svg+xml
    typefinder!(c"image/svg+xml", SECONDARY, svg_type_find, c"svg"),
    // HLS - application/x-hls
    typefinder!(c"application/x-hls", MARGINAL, hls_type_find, c"m3u8"),
    // SDS - audio/x-sds
    typefinder!(c"audio/x-sds", SECONDARY, sds_type_find, c"sds"),
    // Note: typefindfunctions_text registered below with no default caps.
    // Smooth Streaming Manifest - application/vnd.ms-sstr+xml
    typefinder!(
        c"application/vnd.ms-sstr+xml",
        PRIMARY,
        mss_manifest_type_find
    ),
    // URI list - text/uri-list
    typefinder!(c"text/uri-list", MARGINAL, uri_type_find, c"ram"),
    // Legacy game video and audio
    typefinder!(c"video/vivo", SECONDARY, vivo_type_find, c"viv"),
    typefinder!(c"video/x-idcin", MARGINAL, idcin_type_find),
    typefinder!(c"application/x-mm", MARGINAL, mm_type_find),
    typefinder!(c"application/x-smk", MARGINAL, smk_type_find, c"smk"),
    typefinder!(c"application/x-sol", MARGINAL, sol_type_find, c"sol"),
    typefinder!(c"application/x-vmd", MARGINAL, vmd_type_find, c"vmd"),
    typefinder!(
        c"application/x-wc3movie",
        MARGINAL,
        wc3movie_type_find,
        c"wc3movie"
    ),
    typefinder!(c"application/x-wsaud", MARGINAL, wsaud_type_find, c"wsaud"),
    typefinder!(c"application/x-wsvqa", MARGINAL, wsvqa_type_find, c"wsvqa"),
    // Images
    typefinder!(c"application/itc", SECONDARY, itc_type_find, c"itc"),
    typefinder!(
        c"image/x-portable-pixmap",
        SECONDARY,
        pnm_type_find,
        c"pnm,ppm,pgm,pbm",
        c"image/x-portable-bitmap; image/x-portable-graymap; image/x-portable-pixmap; image/x-portable-anymap"
    ),
    typefinder!(c"image/vnd.wap.wbmp", MARGINAL, wbmp_type_find, none, none),
    typefinder!(c"image/x-icon", MARGINAL, windows_icon_type_find, none, none),
    typefinder!(c"image/x-degas", MARGINAL, degas_type_find, none, none),
    start_with!(c"image/vnd.radiance", MARGINAL, c"hdr", [b"#?RADIANCE", b"#?RGBE"], Prob::NEARLY_CERTAIN),
    typefinder!(c"application/x-mcc", PRIMARY, mcc_type_find, c"mcc"),
    typefinder!(c"multipart/x-mixed-replace", SECONDARY, multipart_type_find),
    typefinder!(c"application/x-3gp", PRIMARY, q3gp_type_find, c"3gp"),
    typefinder!(c"video/quicktime", PRIMARY, qt_type_find, c"mov,mp4"),
    typefinder!(
        c"image/x-quicktime",
        SECONDARY,
        qtif_type_find,
        c"qif,qtif,qti"
    ),
    typefinder!(
        c"video/x-h263",
        SECONDARY,
        h263_video_type_find,
        c"h263,263",
        c"video/x-h263, variant=(string)itu"
    ),
    typefinder!(
        c"video/x-h264",
        PRIMARY,
        h264_video_type_find,
        c"h264,x264,264",
        c"video/x-h264,stream-format=byte-stream"
    ),
    typefinder!(
        c"video/x-h265",
        PRIMARY,
        h265_video_type_find,
        c"h265,x265,265",
        c"video/x-h265,stream-format=byte-stream"
    ),
    typefinder!(
        c"video/x-h266",
        PRIMARY,
        h266_video_type_find,
        c"h266,266",
        c"video/x-h266,stream-format=byte-stream"
    ),
    // PXSTR - application/x-pxstr
    typefinder!(c"application/x-pxstr", MARGINAL, pxstr_type_find, c"pxstr"),
    // SSA/ASS - application/x-ssa
    typefinder!(
        c"application/x-ssa",
        SECONDARY,
        subtitles::ssa_type_find,
        c"ssa,ass",
        c"application/x-ssa; application/x-ass"
    ),
    // Matroska - video/x-matroska
    typefinder!(
        c"video/x-matroska",
        PRIMARY,
        matroska_type_find,
        c"mkv,mka,mk3d,webm"
    ),
    // MXF - application/mxf
    typefinder!(c"application/mxf", PRIMARY, mxf_type_find, c"mxf"),
    // AV1 - video/x-av1
    typefinder!(
        c"video/x-av1",
        MARGINAL,
        av1_type_find,
        c"av1",
        c"video/x-av1, stream-format=(string){ obu-stream, annexb }, alignment=(string)none"
    ),
    typefinder!(
        c"video/mpeg-sys",
        PRIMARY,
        mpeg_sys_type_find,
        c"mpe,mpeg,mpg",
        c"video/mpeg, systemstream = (boolean) true, mpegversion = (int) [ 1, 2 ]"
    ),
    typefinder!(
        c"video/mpegts",
        PRIMARY,
        mpeg_ts_type_find,
        c"ts,mts",
        c"video/mpegts, systemstream = (boolean) true, packetsize = (int) [ 188, 208 ]"
    ),
    typefinder!(
        c"video/mpeg-elementary",
        MARGINAL,
        mpeg_video_stream_type_find,
        c"mpv,mpeg,mpg",
        c"video/mpeg, systemstream = (boolean) false"
    ),
    typefinder!(
        c"video/mpeg4",
        PRIMARY,
        mpeg4_video_type_find,
        c"m4v",
        c"video/mpeg, systemstream = (boolean) false"
    ),
    // Content types from the OS-installed shared MIME database, for streams
    // no specific finder claimed.
    typefinder!(c"xdgmime-base", MARGINAL, xdgmime_type_find, none, none),
];

/// The content-type fallback: the installed XDG shared MIME database is
/// consulted for streams no specific finder claimed, and only non-media
/// types are suggested — media formats are what the specific finders
/// above are for. The suggestion is POSSIBLE, so this only beats
/// uncertain results; it exists mainly to keep those false positives away.
///
/// The database is read on first use: loading the text files is cheap
/// compared to doing it for every stream, and plugin init stays fast.
const XDGMIME_PROBE_WINDOW: u64 = 16384;

extern "C" fn xdgmime_type_find(tf: &mut TypeFind, _private: gpointer) {
    static DB: OnceLock<SharedMimeInfo> = OnceLock::new();

    let length = tf.length().map_or(XDGMIME_PROBE_WINDOW, |len| {
        len.get().min(XDGMIME_PROBE_WINDOW)
    });
    let Some(data) = tf.peek_var(0, length as u32) else {
        return;
    };

    let db = DB.get_or_init(SharedMimeInfo::new_magic_only);
    let Some((mime, _priority)) = db.get_mime_type_for_data(data) else {
        return;
    };

    // Media types stay with the specific finders, not even at POSSIBLE.
    if mime.starts_with("audio/") || mime.starts_with("video/") {
        return;
    }

    // Cheap guard against names the typefind machinery cannot express.
    let mut name = mime.bytes();
    if !(name.next().is_some_and(|b| b.is_ascii_alphabetic())
        && name.all(|ch| ch.is_ascii_alphanumeric() || b"/-_.:+".contains(&ch)))
    {
        return;
    }

    let Ok(mime) = CString::new(mime) else {
        return;
    };
    // SAFETY: `tf` is live and `mime` outlives the call.
    unsafe {
        bindings::gst_type_find_suggest_simple(
            &mut tf.0,
            Prob::POSSIBLE.get(),
            mime.as_ptr(),
            ptr::null::<std::os::raw::c_char>(),
        );
    }
}

/// Register all Rust-implemented typefinders with the GStreamer plugin system.
#[unsafe(no_mangle)]
pub extern "C" fn gst_typefindfunctions_rs_register(plugin: &mut GstPlugin) -> gboolean {
    for feature in FEATURES {
        // The engine hands the row back to the shared finder unchanged.
        let (func, data) = feature.finder.registration(feature);

        // NULL on a typo'd caps string.
        // SAFETY: `s` is a static `&CStr`.
        let caps = feature.caps.map_or(ptr::null_mut(), |s| unsafe {
            gst_caps_from_string(s.as_ptr())
        });

        // Return value ignored: only a NULL name fails.
        // SAFETY: `plugin` is live; the row's strings outlive the call.
        unsafe {
            gst_type_find_register(
                plugin,
                feature.name.as_ptr(),
                feature.rank,
                func,
                feature.extensions.map_or(ptr::null(), CStr::as_ptr),
                caps,
                data,
                None,
            );
            // Register takes its own ref on the caps (gst_caps_replace refs via
            // gst_mini_object_replace); release our initial ref from
            // gst_caps_from_string.
            if !caps.is_null() {
                gst_caps_unref(caps);
            }
        }
    }

    // Text factory. One feature stands where upstream registers text/plain,
    // text/utf-16 and text/utf-32 separately: the function cannot know which
    // encoding the bytes carry, and consumers that matched any of the three
    // have always had to re-read the BOM; no default caps, so no type is
    // claimed beyond what the data shows at runtime.
    // SAFETY: `plugin` is live and the strings are static.
    unsafe {
        gst_type_find_register(
            plugin,
            c"typefindfunctions_text".as_ptr(),
            MARGINAL,
            Some(text_type_find),
            c"txt".as_ptr(),
            ptr::null_mut(), // no default caps
            ptr::null_mut(),
            None,
        );
    }

    // Subtitles factory. One feature for the formats the probe tells apart
    // at runtime, where upstream spreads them over this plugin and subparse
    // as one feature each; the per-format caps are suggested per stream.
    // SAFETY: `plugin` is live and the strings are static.
    unsafe {
        gst_type_find_register(
            plugin,
            c"typefindfunctions_subtitles".as_ptr(),
            MARGINAL,
            Some(subtitles::subtitle_type_find),
            c"srt,sub,mpsub,mdvd,smi,txt,dks,vtt".as_ptr(),
            ptr::null_mut(), // no default caps
            ptr::null_mut(),
            None,
        );
    }

    true as gboolean
}

#[cfg(test)]
mod tests {
    use super::{dvdiso_iso9660, dvdiso_type_find, looks_like_text};
    use crate::helpers::Prob;
    use crate::helpers::probe_engine::{probe, probe_suggestions};

    fn iso9660_bytes(pvd: &[u8], junk_at: Option<usize>) -> Vec<u8> {
        let mut data = vec![0u8; 0x8000];
        data.extend_from_slice(pvd);
        if let Some(at) = junk_at {
            data[at] = 0x20;
        }
        data
    }

    fn iso9660(pvd: &[u8], junk_at: Option<usize>) -> bool {
        probe(&iso9660_bytes(pvd, junk_at), dvdiso_iso9660)
    }

    /// A volume whose system area is empty and whose descriptor opens
    /// with ECMA-119's type octet and identifier answers, however long
    /// the file runs on behind it.
    #[test]
    fn an_empty_iso9660_system_area_answers() {
        assert!(iso9660(b"\x01CD001", None));
        assert!(iso9660(b"\x01CD001\x01Volume space", None));
        // A file ending on the descriptor's last octet is enough; one
        // octet shorter holds no descriptor.
        let mut exact = vec![0u8; 0x8000];
        exact.extend(b"\x01CD001");
        assert!(probe(&exact, dvdiso_iso9660));
        exact.pop();
        assert!(!probe(&exact, dvdiso_iso9660));
    }

    /// The system area is read to its last octet: one byte set anywhere
    /// in the thirty-two KiB dismisses the file, and so does a descriptor
    /// that differs in its type octet or its identifier.
    #[test]
    fn one_foreign_octet_dismisses_the_volume() {
        for at in [0, 1, 0x1000, 0x7FFF] {
            assert!(!iso9660(b"\x01CD001", Some(at)), "octet {at} set");
        }
        assert!(!iso9660(b"\x00CD001", None));
        assert!(!iso9660(b"\x02CD001", None));
        assert!(!iso9660(b"\x01CD002", None));
        assert!(!iso9660(b"\x01CD00", None)); // the identifier runs off the file
        assert!(!iso9660(b"", None)); // zeros alone: nothing behind them
        assert!(!probe(b"", dvdiso_iso9660));
    }

    /// The whole typefinding function answers at likely, with the
    /// shared octet-stream caps; a file with one octet set in the
    /// system area suggests nothing.
    #[test]
    fn dvdiso_suggests_at_likely() {
        let suggestions = probe_suggestions(&iso9660_bytes(b"\x01CD001", None), |tf| {
            dvdiso_type_find(tf, std::ptr::null_mut())
        });
        assert_eq!(
            suggestions,
            [(Prob::LIKELY.get(), "application/octet-stream".to_string())]
        );
        assert!(
            probe_suggestions(&iso9660_bytes(b"\x01CD001", Some(0x1234)), |tf| {
                dvdiso_type_find(tf, std::ptr::null_mut())
            })
            .is_empty()
        );
    }

    #[test]
    fn text_rule() {
        // Includes a character the probe window cut in half.
        let text: [&[u8]; 3] = [
            b"hello world\n",
            "naïve café 日本語".as_bytes(),
            &"café".as_bytes()[..4],
        ];
        for d in text {
            assert!(looks_like_text(d), "should be text: {d:?}");
        }
        // NUL at the edge and in the middle, and ill-formed UTF-8: stray
        // continuation, overlong, surrogate.
        let binary: [&[u8]; 6] = [
            &[0; 64],
            b"ab\0cd\0ef",
            b"nul at the edge\0",
            b"abcd\x80efgh",
            b"abcd\xC0\x80efgh",
            b"abcd\xED\xA0\x80efgh",
        ];
        for d in binary {
            assert!(!looks_like_text(d), "should not be text: {d:?}");
        }
    }

    /// Rank is observable behaviour: it breaks ties between finders and
    /// gst-inspect prints it. audio/x-au once drifted to a stray literal.
    #[test]
    fn au_is_registered_at_marginal() {
        use super::{FEATURES, MARGINAL};
        assert!(
            FEATURES
                .iter()
                .any(|f| f.name == c"audio/x-au" && f.rank == MARGINAL)
        );
    }

    /// The magic bytes and probability each `start_with!` row must carry,
    /// taken from the per-format functions the table replaced. Kept apart from
    /// `FEATURES` so that a typo in either place fails the test below.
    const START_WITH_EXPECTED: &[(&str, &[&[u8]], Prob)] = &[
        ("audio/midi", &[b"MThd"], Prob::MAXIMUM),
        ("audio/x-au", &[b".snd", b"dns."], Prob::MAXIMUM),
        ("application/x-ape", &[b"MAC "], Prob::from_raw(90)),
        ("audio/x-paris", &[b" paf", b"fap "], Prob::MAXIMUM),
        ("audio/x-celt", &[b"CELT    "], Prob::MAXIMUM),
        ("audio/iLBC-sh", &[b"#!iLBC30", b"#!iLBC20"], Prob::LIKELY),
        (
            "audio/x-tap-tap",
            &[b"C64-TAPE-RAW", b"C16-TAPE-RAW"],
            Prob::MAXIMUM,
        ),
        ("audio/x-ttafile", &[b"TTA"], Prob::MAXIMUM),
        ("video/x-flv", &[b"FLV"], Prob::MAXIMUM),
        ("image/gif", &[b"GIF8"], Prob::MAXIMUM),
        ("application/x-gzip", &[b"\x1f\x8b"], Prob::LIKELY),
        ("application/zip", &[b"PK\x03\x04"], Prob::LIKELY),
        ("audio/x-imelody", &[b"BEGIN:IMELODY"], Prob::MAXIMUM),
        ("application/x-scc", &[b"Scenarist_SCC V1.0"], Prob::MAXIMUM),
        ("application/vnd.rn-realmedia", &[b".RMF"], Prob::MAXIMUM),
        ("application/x-pn-realaudio", &[b".ra\xfd"], Prob::MAXIMUM),
        ("audio/x-nist", &[b"NIST"], Prob::MAXIMUM),
        ("audio/x-voc", &[b"Creative"], Prob::MAXIMUM),
        ("audio/x-rf64", &[b"RF64"], Prob::MAXIMUM),
        ("audio/x-amr-nb-sh", &[b"#!AMR"], Prob::LIKELY),
        ("audio/x-amr-wb-sh", &[b"#!AMR-WB"], Prob::MAXIMUM),
        ("audio/x-w64", &[b"riff"], Prob::MAXIMUM),
        ("image/x-xpixmap", &[b"/* XPM */"], Prob::MAXIMUM),
        ("image/x-xbitmap", &[b"/* XBM */"], Prob::MAXIMUM),
        ("image/x-farbfeld", &[b"farbfeld"], Prob::NEARLY_CERTAIN),
        ("image/qoi", &[b"qoif"], Prob::NEARLY_CERTAIN),
        ("image/vnd.ms-dds", &[b"DDS "], Prob::NEARLY_CERTAIN),
        ("audio/x-sid", &[b"PSID"], Prob::MAXIMUM),
        ("image/x-xcf", &[b"gimp xcf"], Prob::MAXIMUM),
        ("application/x-bzip", &[b"BZh"], Prob::LIKELY),
        ("application/x-compress", &[b"\x1f\x9d"], Prob::LIKELY),
        ("application/x-executable", &[b"\x7fELF"], Prob::MAXIMUM),
        (
            "audio/x-spc",
            &[b"SNES-SPC700 Sound File Data"],
            Prob::MAXIMUM,
        ),
        ("audio/x-caf", &[b"caff\x00\x01"], Prob::MAXIMUM),
        ("application/x-rar", &[b"Rar!"], Prob::LIKELY),
        ("audio/x-nsf", &[b"NESM\x1a"], Prob::MAXIMUM),
        ("audio/x-gym", &[b"GYMX"], Prob::MAXIMUM),
        ("audio/x-ay", &[b"ZXAYEMUL"], Prob::MAXIMUM),
        ("audio/x-gbs", &[b"GBS\x01"], Prob::MAXIMUM),
        ("audio/x-vgm", &[b"Vgm "], Prob::MAXIMUM),
        ("audio/x-sap", &[b"SAP\r\nAUTHOR "], Prob::MAXIMUM),
        ("video/x-ivf", &[b"DKIF"], Prob::NEARLY_CERTAIN),
        ("audio/x-kss", &[b"KSSX\x00"], Prob::MAXIMUM),
        ("application/pdf", &[b"%PDF-"], Prob::LIKELY),
        (
            "application/octet-stream",
            &[b"\x00\x00\x00\x01Bud1"],
            Prob::LIKELY,
        ),
        ("audio/x-tap-dmp", &[b"DC2N-TAP-RAW"], Prob::LIKELY),
        (
            "image/vnd.adobe.photoshop",
            &[b"8BPS\x00\x01\x00\x00\x00\x00"],
            Prob::LIKELY,
        ),
        ("audio/x-xi", &[b"Extended Instrument: "], Prob::MAXIMUM),
        ("video/x-dirac", &[b"BBCD", b"KW-DIRAC"], Prob::MAXIMUM),
        ("video/x-theora", &[b"\x80theora"], Prob::MAXIMUM),
        (
            "image/vnd.radiance",
            &[b"#?RADIANCE", b"#?RGBE"],
            Prob::NEARLY_CERTAIN,
        ),
        (
            "application/x-shockwave-flash",
            &[b"FWS", b"CWS"],
            Prob::MAXIMUM,
        ),
        (
            "video/x-ms-asf",
            &[b"\x30\x26\xb2\x75\x8e\x66\xcf\x11\xa6\xd9\x00\xaa\x00\x62\xce\x6c"],
            Prob::MAXIMUM,
        ),
        (
            "video/x-mve",
            &[b"Interplay MVE File\x1a\x00\x1a\x00\x00\x01\x33\x11"],
            Prob::MAXIMUM,
        ),
        ("video/x-mng", &[b"\x8aMNG\r\n\x1a\n"], Prob::MAXIMUM),
        ("image/x-jng", &[b"\x8bJNG\r\n\x1a\n"], Prob::MAXIMUM),
        ("image/x-sun-raster", &[b"\x59\xa6\x6a\x95"], Prob::MAXIMUM),
        (
            "application/msword",
            &[b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1"],
            Prob::LIKELY,
        ),
        ("video/x-avs", &[b"\x77\x57\x10\x00"], Prob::LIKELY),
        (
            "video/x-nuv",
            &[b"MythTVVideo", b"NuppelVideo"],
            Prob::MAXIMUM,
        ),
        (
            "application/x-ogm-video",
            &[b"\x01video\x00\x00\x00"],
            Prob::MAXIMUM,
        ),
        (
            "application/x-ogm-audio",
            &[b"\x01audio\x00\x00\x00"],
            Prob::MAXIMUM,
        ),
        (
            "application/x-ogm-text",
            &[b"\x01text\x00\x00\x00\x00"],
            Prob::MAXIMUM,
        ),
    ];

    /// Every `start_with!` row answers for its own magic, and for nothing else.
    ///
    /// These formats' magic bytes, probability and caps are table data, so no
    /// other test looks at them. The expectations above are separate from the
    /// table: a flipped byte, a wrong probability or a row whose data drifted
    /// from its function would otherwise silently drop a format.
    #[test]
    fn start_with_rows_answer_for_their_magic_only() {
        use super::FEATURES;
        use crate::helpers::Finder;

        let mut checked = 0;
        for &(name, magics, prob) in START_WITH_EXPECTED {
            let row = FEATURES
                .iter()
                .find(|f| f.name.to_str().unwrap() == name)
                .unwrap_or_else(|| panic!("no row for {name}"));
            let Finder::StartWith {
                magic: row_magic,
                prob: row_prob,
            } = &row.finder
            else {
                panic!("{name} is not a start_with row")
            };
            assert_eq!(*row_magic, magics, "{name}");
            assert_eq!(*row_prob, prob, "{name}");

            // Drive the finder exactly as registration would.
            let (func, data_ptr) = row.finder.registration(row);
            let func = func.expect("start_with row has a finder");

            let magic = magics[0];
            let mut stream = magic.to_vec();
            stream.resize(magic.len() + 8, 0);

            let got = probe_suggestions(&stream, |tf| func(tf, data_ptr));
            assert_eq!(got, vec![(prob.get(), name.to_owned())], "{name}");

            // One byte off the magic: nothing is recognised.
            let mut wrong = stream.clone();
            wrong[magic.len() - 1] ^= 0xFF;
            let got = probe_suggestions(&wrong, |tf| func(tf, data_ptr));
            assert!(got.is_empty(), "{name} matched a corrupted magic");

            // One byte short: the engine cannot serve the peek, so again
            // nothing is recognised.
            let got = probe_suggestions(&stream[..magic.len() - 1], |tf| func(tf, data_ptr));
            assert!(got.is_empty(), "{name} matched a truncated magic");

            checked += 1;
        }
        // No `start_with!` row may escape the expectations above.
        let rows = FEATURES
            .iter()
            .filter(|f| matches!(f.finder, Finder::StartWith { .. }))
            .count();
        assert_eq!(rows, checked, "one expectation per start_with! row");
    }
}
