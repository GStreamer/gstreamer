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
use std::ptr;

use crate::bindings::gpointer;
use crate::helpers::{
    Feature, Prob, Scanner, TypeFind, magic_eq, opt, read_u16_le, read_u32_le, suggest, typefinder,
};
#[allow(unused_imports)]
use bindings::{
    GST_RANK_MARGINAL as MARGINAL, GST_RANK_PRIMARY as PRIMARY, GST_RANK_SECONDARY as SECONDARY,
};

// MIDI — audio/midi
// Magic `MThd` @ 0.
extern "C" fn mid_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"MThd") {
        suggest!(tf, Prob::MAXIMUM, c"audio/midi");
    }
}

// AU — audio/x-au
// Magic `.snd` or `dns.` @ 0.
extern "C" fn au_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b".snd") || tf.starts_with(b"dns.") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-au");
    }
}

// APE — application/x-ape
// Magic `MAC ` @ 0.
extern "C" fn ape_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"MAC ") {
        suggest!(tf, 90, c"application/x-ape");
    }
}

// SHN — audio/x-shorten
// Magic `ajkg` @ 0 or `SHNAMPSK` at file tail (−8).
extern "C" fn shn_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"ajkg") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-shorten");
    } else if let Some(data) = tf.peek_var(-8, 8)
        && magic_eq(data, b"SHNAMPSK")
    {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-shorten");
    }
}

// PARIS — audio/x-paris
// Magic ` paf` or `fap ` @ 0.
extern "C" fn paris_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b" paf") || tf.starts_with(b"fap ") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-paris");
    }
}

// CELT — audio/x-celt
// Magic `CELT    ` (8 bytes with trailing spaces) @ 0.
extern "C" fn celt_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"CELT    ") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-celt");
    }
}

// SVX — audio/x-svx
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

// iLBC — audio/iLBC-sh
// Magic `#!iLBC30` or `#!iLBC20` @ 0.
extern "C" fn ilbc_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"#!iLBC30") || tf.starts_with(b"#!iLBC20") {
        suggest!(tf, Prob::LIKELY, c"audio/iLBC-sh");
    }
}

// DSF — audio/x-dsf
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

// AIFF — audio/x-aiff
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

// TAP — audio/x-tap-tap
// Magic `C64-TAPE-RAW` or `C16-TAPE-RAW` @ 0.
extern "C" fn tap_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"C64-TAPE-RAW") || tf.starts_with(b"C16-TAPE-RAW") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-tap-tap");
    }
}

// TTA — audio/x-ttafile
// Magic `TTA` @ 0.
extern "C" fn tta_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"TTA") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-ttafile");
    }
}

extern "C" fn swf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<3>(0)
        && (data == b"FWS" || data == b"CWS")
    {
        suggest!(tf, Prob::MAXIMUM, c"application/x-shockwave-flash");
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

extern "C" fn flv_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"FLV") {
        suggest!(tf, Prob::MAXIMUM, c"video/x-flv");
    }
}

extern "C" fn gif_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"GIF8") {
        suggest!(tf, Prob::MAXIMUM, c"image/gif");
    }
}

extern "C" fn gz_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"\x1f\x8b") {
        suggest!(tf, Prob::LIKELY, c"application/x-gzip");
    }
}

extern "C" fn zip_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"PK\x03\x04") {
        suggest!(tf, Prob::LIKELY, c"application/zip");
    }
}

extern "C" fn ar_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<32>(0)
        && data[..7] == *b"!<arch>"
        && data[7] == b'\n'
    {
        // First member header starts at offset 8; file name field (16 bytes) is @8–23.
        let name_valid = (8..=23).all(|i| data[i].is_ascii_graphic() || data[i] == b' ');
        if name_valid {
            suggest!(tf, Prob::NEARLY_CERTAIN, c"application/x-ar");
        } else {
            suggest!(tf, Prob::POSSIBLE, c"application/x-ar");
        }
    }
}

extern "C" fn asf_type_find(tf: &mut TypeFind, _private: gpointer) {
    const MAGIC: [u8; 16] = [
        0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce,
        0x6c,
    ];
    if tf.starts_with(&MAGIC) {
        suggest!(tf, Prob::MAXIMUM, c"video/x-ms-asf");
    }
}

extern "C" fn cmml_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<10>(0)
        && data[..8] == *b"CMML\0\0\0\0"
        && read_u16_le(&data[8..]).is_some_and(|v| v >= 2)
    {
        suggest!(tf, Prob::MAXIMUM, c"text/x-cmml");
    }
}

extern "C" fn webvtt_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<10>(0) {
        // Check for WEBVTT with optional BOM prefix.
        let (magic_off, magic_end) =
            if data.len() >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf {
                (3, 9) // UTF-8 BOM + WEBVTT + separator
            } else {
                (0, 6) // No BOM: WEBVTT + separator
            };

        if magic_off + 6 > data.len() {
            return;
        }
        if !magic_eq(&data[magic_off..magic_off + 6], b"WEBVTT") {
            return;
        }

        // Separator must be space, tab, LF, or CR.
        let sep = data[magic_end];
        if matches!(sep, b' ' | b'\t' | b'\n' | b'\r') {
            suggest!(tf, Prob::MAXIMUM, c"application/x-subtitle-vtt", [
                (c"parsed", bool : false),
            ]);
        }
    }
}

// VCD — video/x-vcd
// MPEG-1 disc image magic at offset 0.
extern "C" fn vcd_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(data) = tf.peek::<12>(0)
        && data[0] == 0x00
        && (1..10).all(|i| data[i] == 0xff)
        && data[10] == 0xff
        && data[11] == 0x00
    {
        suggest!(tf, Prob::MAXIMUM, c"video/x-vcd");
    }
}

// iMelody — audio/x-imelody
extern "C" fn imelody_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"BEGIN:IMELODY") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-imelody");
    }
}

// SCC — application/x-scc
extern "C" fn scc_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"Scenarist_SCC V1.0") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-scc");
    }
}

// RMF — application/vnd.rn-realmedia
extern "C" fn rmf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b".RMF") {
        suggest!(tf, Prob::MAXIMUM, c"application/vnd.rn-realmedia");
    }
}

// RAM — application/x-pn-realaudio
// `.ra\xfd` @ 0.
extern "C" fn ram_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b".ra\xfd") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-pn-realaudio");
    }
}

// NIST — audio/x-nist
extern "C" fn nist_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"NIST") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-nist");
    }
}

// VOC — audio/x-voc
extern "C" fn voc_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"Creative") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-voc");
    }
}

// RF64 — audio/x-rf64
extern "C" fn rf64_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"RF64") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-rf64");
    }
}

// AMR — audio/x-amr-nb-sh
extern "C" fn amr_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"#!AMR") {
        suggest!(tf, Prob::LIKELY, c"audio/x-amr-nb-sh");
    }
}

// AMR WB — audio/x-amr-wb-sh
extern "C" fn amr_wb_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"#!AMR-WB") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-amr-wb-sh");
    }
}

// MVE — video/x-mve
// "Interplay MVE File" (18 bytes) + 1a 00 1a 00 00 01 33 11 @18–25.
const MVE_MAGIC: [u8; 26] = [
    b'I', b'n', b't', b'e', b'r', b'p', b'l', b'a', b'y', b' ', b'M', b'V', b'E', b' ', b'F', b'i',
    b'l', b'e', 0x1a, 0x00, 0x1a, 0x00, 0x00, 0x01, 0x33, 0x11,
];
extern "C" fn mve_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&MVE_MAGIC) {
        suggest!(tf, Prob::MAXIMUM, c"video/x-mve");
    }
}

// W64 — audio/x-w64
extern "C" fn w64_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"riff") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-w64");
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

// XPM — image/x-xpixmap
extern "C" fn xpm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"/* XPM */") {
        suggest!(tf, Prob::MAXIMUM, c"image/x-xpixmap");
    }
}

extern "C" fn xbm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"/* XBM */") {
        suggest!(tf, Prob::MAXIMUM, c"image/x-xbitmap");
    }
}

extern "C" fn farbfeld_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"farbfeld") {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"image/x-farbfeld");
    }
}
extern "C" fn qoif_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"qoif") {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"image/qoi");
    }
}
extern "C" fn dds_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"DDS ") {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"image/vnd.ms-dds");
    }
}

// Y4M — application/x-yuv4mpeg
// "YUV4MPEG2 " includes the trailing space; the version is always 2.
extern "C" fn y4m_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"YUV4MPEG2 ") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-yuv4mpeg", [
            (c"y4mversion", i32 : 2),
        ]);
    }
}

// SID — audio/x-sid
extern "C" fn sid_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"PSID") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-sid");
    }
}

// XCF — image/x-xcf
extern "C" fn xcf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"gimp xcf") {
        suggest!(tf, Prob::MAXIMUM, c"image/x-xcf");
    }
}

// MNG — video/x-mng
const MNG_MAGIC: [u8; 8] = [0x8a, 0x4d, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
extern "C" fn mng_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&MNG_MAGIC) {
        suggest!(tf, Prob::MAXIMUM, c"video/x-mng");
    }
}

// JNG — image/x-jng
const JNG_MAGIC: [u8; 8] = [0x8b, 0x4a, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
extern "C" fn jng_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&JNG_MAGIC) {
        suggest!(tf, Prob::MAXIMUM, c"image/x-jng");
    }
}

// RAS — image/x-sun-raster
const RAS_MAGIC: [u8; 4] = [0x59, 0xa6, 0x6a, 0x95];
extern "C" fn ras_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&RAS_MAGIC) {
        suggest!(tf, Prob::MAXIMUM, c"image/x-sun-raster");
    }
}

// BZIP2 — application/x-bzip
extern "C" fn bz2_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"BZh") {
        suggest!(tf, Prob::LIKELY, c"application/x-bzip");
    }
}

// COMPRESS — application/x-compress
extern "C" fn z_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"\x1f\x9d") {
        suggest!(tf, Prob::LIKELY, c"application/x-compress");
    }
}

// ELF — application/x-executable
extern "C" fn elf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"\x7fELF") {
        suggest!(tf, Prob::MAXIMUM, c"application/x-executable");
    }
}

// SPC — audio/x-spc
extern "C" fn spc_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"SNES-SPC700 Sound File Data") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-spc");
    }
}

// CAF — audio/x-caf
extern "C" fn caf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"caff\x00\x01") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-caf");
    }
}

// RAR — application/x-rar
extern "C" fn rar_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"Rar!") {
        suggest!(tf, Prob::LIKELY, c"application/x-rar");
    }
}

// NSF — audio/x-nsf
extern "C" fn nsf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"NESM\x1a") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-nsf");
    }
}

// GYM — audio/x-gym
extern "C" fn gym_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"GYMX") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-gym");
    }
}

// AY — audio/x-ay
extern "C" fn ay_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"ZXAYEMUL") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-ay");
    }
}

// GBS — audio/x-gbs
extern "C" fn gbs_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"GBS\x01") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-gbs");
    }
}

// VGM — audio/x-vgm
extern "C" fn vgm_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"Vgm ") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-vgm");
    }
}

// SAP — audio/x-sap
extern "C" fn sap_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"SAP\r\nAUTHOR ") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-sap");
    }
}

// IVF — video/x-ivf
extern "C" fn ivf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"DKIF") {
        suggest!(tf, Prob::NEARLY_CERTAIN, c"video/x-ivf");
    }
}

// KSS — audio/x-kss
extern "C" fn kss_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"KSSX\x00") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-kss");
    }
}

// PDF — application/pdf
extern "C" fn pdf_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"%PDF-") {
        suggest!(tf, Prob::LIKELY, c"application/pdf");
    }
}

// DOC — application/msword (legacy .doc)
const DOC_MAGIC: [u8; 8] = [0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1];
extern "C" fn doc_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&DOC_MAGIC) {
        suggest!(tf, Prob::LIKELY, c"application/msword");
    }
}

// DS_Store — application/octet-stream
extern "C" fn ds_store_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"\x00\x00\x00\x01Bud1") {
        suggest!(tf, Prob::LIKELY, c"application/octet-stream");
    }
}

// DMP — audio/x-tap-dmp (Tape2029)
extern "C" fn dmp_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"DC2N-TAP-RAW") {
        suggest!(tf, Prob::LIKELY, c"audio/x-tap-dmp");
    }
}

// AVS — video/x-avs
const AVS_MAGIC: [u8; 4] = [0x77, 0x57, 0x10, 0x00];
extern "C" fn avs_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(&AVS_MAGIC) {
        suggest!(tf, Prob::LIKELY, c"video/x-avs");
    }
}

// PSD — image/vnd.adobe.photoshop
extern "C" fn psd_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"8BPS\x00\x01\x00\x00\x00\x00") {
        suggest!(tf, Prob::LIKELY, c"image/vnd.adobe.photoshop");
    }
}

// XI — audio/x-xi
extern "C" fn xi_type_find(tf: &mut TypeFind, _private: gpointer) {
    if tf.starts_with(b"Extended Instrument: ") {
        suggest!(tf, Prob::MAXIMUM, c"audio/x-xi");
    }
}

// =============================================================================
// Complex finder — BMP (DIB structure validation)
// =============================================================================

// BMP — image/bmp
// Magic "BM" @0, DIB header validation at offsets 14–28/32.
extern "C" fn bmp_type_find(tf: &mut TypeFind, _private: gpointer) {
    const TEN_MIB: u32 = 10 * 1024 * 1024;
    const MAX_DIM: u32 = 0xFFFFF;

    let Some(data) = tf.peek::<32>(0) else { return };

    if data[0] != b'B' || data[1] != b'M' || data[6..10].iter().any(|&b| b != 0) {
        return;
    }
    if read_u32_le(&data[10..]).unwrap() > TEN_MIB {
        return;
    }

    let (w, h, planes, bpp) = match read_u32_le(&data[14..]).unwrap() {
        0x0C => {
            // BITMAPCOREHEADER — w,h as u16.
            if let (Some(w), Some(h), Some(planes), Some(bpp)) = (
                read_u16_le(&data[18..]),
                read_u16_le(&data[20..]),
                read_u16_le(&data[22..]),
                read_u16_le(&data[24..]),
            ) {
                (u32::from(w), u32::from(h), planes, bpp)
            } else {
                return;
            }
        }
        40 | 64 | 108 | 124 | 0xF0 => {
            // BITMAPINFOHEADER V3–V4 — w,h as u32.
            if let (Some(w), Some(h), Some(planes), Some(bpp)) = (
                read_u32_le(&data[18..]),
                read_u32_le(&data[22..]),
                read_u16_le(&data[26..]),
                read_u16_le(&data[28..]),
            ) {
                (w, h, planes, bpp)
            } else {
                return;
            }
        }
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

// PNG — image/png
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

// JPEG — image/jpeg
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
        // TEM, RST0–7, SOI and EOI carry no length.
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

// =============================================================================
// Registration table and entry point
// =============================================================================

/// Registration table: one entry per typefinder.
/// The `typefinder!` macro expands to a `Feature` tuple.
///
/// Signatures:
/// - `typefinder!("mime/type", RANK, func_name)` — no extensions
/// - `typefinder!("mime/type", RANK, func_name, "exts")` — with extensions
/// - `typefinder!("mime/type", RANK, func_name, "exts", "custom caps")` — custom caps
///
/// - `"mime/type"`  – feature name and default caps
/// - `RANK`         – PRIMARY, SECONDARY, MARGINAL (or offset like PRIMARY+103)
/// - `func_name`    – the typefind function, or `none` for extension-only registration
/// - `"exts"`       – comma-separated extensions string (optional)
const FEATURES: &[Feature] = &[
    // Trivial finders — magic bytes only
    typefinder!(
        "application/x-shockwave-flash",
        SECONDARY,
        swf_type_find,
        "swf,swfl"
    ),
    typefinder!(
        "image/tiff",
        PRIMARY,
        tiff_type_find,
        "tif,tiff",
        "image/tiff, endianness = (int) { BIG_ENDIAN, LITTLE_ENDIAN }"
    ),
    typefinder!("video/x-flv", SECONDARY, flv_type_find, "flv"),
    typefinder!("image/gif", PRIMARY, gif_type_find, "gif"),
    typefinder!("application/x-gzip", SECONDARY, gz_type_find, "gz"),
    typefinder!("application/zip", SECONDARY, zip_type_find, "zip"),
    typefinder!("application/x-ar", SECONDARY, ar_type_find, "a"),
    typefinder!("video/x-ms-asf", SECONDARY, asf_type_find, "asf,wm,wma,wmv"),
    typefinder!("text/x-cmml", PRIMARY, cmml_type_find),
    typefinder!(
        "application/x-subtitle-vtt",
        MARGINAL,
        webvtt_type_find,
        "vtt",
        "application/x-subtitle-vtt, parsed=(boolean)false"
    ),
    // Complex finders — structure validation and chunk/marker walking
    typefinder!("image/bmp", PRIMARY, bmp_type_find, "bmp"),
    typefinder!("image/png", PRIMARY + 14, png_type_find, "png"),
    typefinder!("image/jpeg", PRIMARY + 15, jpeg_type_find, "jpg,jpe,jpeg"),
    // Additional startswith finders
    typefinder!("audio/midi", PRIMARY, mid_type_find, "mid,midi"),
    typefinder!("audio/x-ttafile", PRIMARY, tta_type_find, "tta"),
    typefinder!("application/x-ape", SECONDARY, ape_type_find, "ape"),
    typefinder!("audio/x-au", 1, au_type_find, "au,snd"),
    typefinder!("audio/x-shorten", SECONDARY, shn_type_find, "shn"),
    typefinder!("audio/x-paris", SECONDARY, paris_type_find, "paf"),
    typefinder!("audio/x-celt", PRIMARY, celt_type_find),
    typefinder!("audio/x-svx", SECONDARY, svx_type_find, "iff,svx"),
    typefinder!("audio/iLBC-sh", PRIMARY, ilbc_type_find, "ilbc"),
    typefinder!("audio/x-dsf", PRIMARY, dsf_type_find),
    typefinder!("audio/x-aiff", SECONDARY, aiff_type_find, "aiff,aif,aifc"),
    typefinder!("audio/x-tap-tap", PRIMARY, tap_type_find, "tap"),
    typefinder!("audio/x-amr-nb-sh", PRIMARY, amr_type_find, "amr"),
    typefinder!("audio/x-amr-wb-sh", PRIMARY, amr_wb_type_find, "amr"),
    typefinder!("video/x-vcd", PRIMARY, vcd_type_find, "dat"),
    typefinder!(
        "audio/x-imelody",
        PRIMARY,
        imelody_type_find,
        "imy,ime,imelody"
    ),
    typefinder!("application/x-yuv4mpeg", PRIMARY, y4m_type_find, "y4m"),
    typefinder!("application/x-scc", PRIMARY, scc_type_find, "scc"),
    typefinder!(
        "application/vnd.rn-realmedia",
        SECONDARY,
        rmf_type_find,
        "ra,ram,rm,rmvb"
    ),
    typefinder!(
        "application/x-pn-realaudio",
        SECONDARY,
        ram_type_find,
        "ra,ram,rm,rmvb"
    ),
    typefinder!("audio/x-nist", SECONDARY, nist_type_find, "nist"),
    typefinder!("audio/x-voc", SECONDARY, voc_type_find, "voc"),
    typefinder!("audio/x-rf64", PRIMARY, rf64_type_find, "rf64"),
    typefinder!("audio/x-sid", MARGINAL, sid_type_find, "sid"),
    typefinder!("image/x-xcf", SECONDARY, xcf_type_find, "xcf"),
    typefinder!("video/x-mng", SECONDARY, mng_type_find, "mng"),
    typefinder!("video/x-mve", SECONDARY, mve_type_find, "mve"),
    typefinder!("image/x-jng", SECONDARY, jng_type_find, "jng"),
    typefinder!("image/x-sun-raster", SECONDARY, ras_type_find, "ras"),
    typefinder!("application/x-bzip", SECONDARY, bz2_type_find, "bz2"),
    typefinder!("application/x-compress", SECONDARY, z_type_find, "Z"),
    typefinder!("application/x-executable", MARGINAL, elf_type_find),
    typefinder!("audio/x-spc", SECONDARY, spc_type_find, "spc"),
    typefinder!("audio/x-caf", SECONDARY, caf_type_find, "caf"),
    typefinder!("application/x-rar", SECONDARY, rar_type_find, "rar"),
    typefinder!("audio/x-nsf", SECONDARY, nsf_type_find, "nsf"),
    typefinder!("audio/x-gym", SECONDARY, gym_type_find, "gym"),
    typefinder!("audio/x-ay", SECONDARY, ay_type_find, "ay"),
    typefinder!("audio/x-gbs", SECONDARY, gbs_type_find, "gbs"),
    typefinder!("audio/x-vgm", SECONDARY, vgm_type_find, "vgm"),
    typefinder!("audio/x-w64", SECONDARY, w64_type_find, "w64"),
    typefinder!("audio/x-sap", SECONDARY, sap_type_find, "sap"),
    typefinder!("video/x-ivf", SECONDARY, ivf_type_find, "ivf"),
    typefinder!("audio/x-kss", SECONDARY, kss_type_find, "kss"),
    typefinder!("application/pdf", SECONDARY, pdf_type_find, "pdf"),
    typefinder!("application/msword", SECONDARY, doc_type_find, "doc"),
    // RIFF variants
    typefinder!("video/x-4xm", PRIMARY, fourxm_type_find),
    typefinder!("video/x-msvideo", PRIMARY, avi_type_find, "avi,avf"),
    typefinder!("audio/qcelp", PRIMARY, qcp_type_find, "qcp"),
    typefinder!("video/x-cdxa", PRIMARY, cdxa_type_find, "dat"),
    typefinder!("audio/riff-midi", PRIMARY, riff_mid_type_find, "mid,midi"),
    typefinder!("audio/x-wav", PRIMARY, wav_type_find, "wav"),
    typefinder!("image/webp", PRIMARY, webp_type_find, "webp"),
    typefinder!("audio/x-xwma", PRIMARY, xwma_type_find),
    typefinder!(
        "application/octet-stream",
        SECONDARY,
        ds_store_type_find,
        "DS_Store"
    ),
    typefinder!("audio/x-tap-dmp", SECONDARY, dmp_type_find, "dmp"),
    typefinder!("video/x-avs", SECONDARY, avs_type_find),
    typefinder!("image/vnd.adobe.photoshop", SECONDARY, psd_type_find, "psd"),
    typefinder!("audio/x-xi", SECONDARY, xi_type_find, "xi"),
    typefinder!("image/x-xpixmap", SECONDARY, xpm_type_find, "xpm"),
    typefinder!("image/x-xbitmap", SECONDARY, xbm_type_find, "xbm"),
    typefinder!("image/x-farbfeld", SECONDARY, farbfeld_type_find, "ff"),
    typefinder!("image/qoi", SECONDARY, qoif_type_find, "qoi"),
    typefinder!("image/vnd.ms-dds", SECONDARY, dds_type_find, "dds"),
];

/// Register all Rust-implemented typefinders with the GStreamer plugin system.
#[unsafe(no_mangle)]
pub extern "C" fn gst_typefindfunctions_rs_register(plugin: &mut GstPlugin) -> gboolean {
    let cs = |s: &str| CString::new(s).expect("NULL in registration table string");

    for &Feature(name, rank, func, extensions, caps_str) in FEATURES {
        let name = cs(name);
        let extensions = extensions.map(cs);
        let caps_str = caps_str.map(cs);

        // NULL on a typo'd caps string, as in C.
        let caps = caps_str.map_or(ptr::null_mut(), |s| unsafe {
            gst_caps_from_string(s.as_ptr())
        });
        // Return value ignored, as in C: only a NULL name fails.
        unsafe {
            gst_type_find_register(
                plugin,
                name.as_ptr(),
                rank,
                func,
                extensions.as_deref().map_or(ptr::null(), CStr::as_ptr),
                caps,
                ptr::null_mut(),
                None,
            );
            // Register takes its own ref on the caps (gst_caps_replace refs via
            // gst_mini_object_replace); release our initial ref from
            // gst_caps_from_string, as C's sw_data_destroy does.
            if !caps.is_null() {
                gst_caps_unref(caps);
            }
        }
    }
    true as gboolean
}
