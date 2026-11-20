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

use std::ffi::CStr;
use std::ptr;

use crate::helpers::{Feature, Finder, Prob, opt, start_with, typefinder};
// The table names the uncategorised finders by their plain identifiers.
use crate::misc::*;
use crate::subtitles;
#[allow(unused_imports)]
use bindings::{
    GST_RANK_MARGINAL as MARGINAL, GST_RANK_PRIMARY as PRIMARY, GST_RANK_SECONDARY as SECONDARY,
};

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
    use crate::helpers::Prob;
    use crate::helpers::probe_engine::probe_suggestions;

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
