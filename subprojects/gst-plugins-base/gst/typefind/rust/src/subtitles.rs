// GStreamer
//
// Copyright (C) 2026 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Subtitle typefinders.
//!
//! A single factory that handles all subtitle caps: read the first 128 bytes, normalize the
//! encoding to UTF-8, and match against the known formats.

use std::ffi::CStr;

use crate::bindings::gpointer;
use crate::helpers::{Prob, TypeFind, convert_to_utf8, locale_charset, suggest};

/// Supported subtitle formats.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Format {
    MicroDvd,
    SubRip,
    Dks,
    WebVtt,
    MpSub,
    Sami,
    TmPlayer,
    Mpl2,
    SubViewer,
    QtText,
    Lrc,
}

impl Format {
    /// Media type of the format, and the `format=` value to go with it. That
    /// field exists only for `application/x-subtitle`, so it is set for the
    /// formats that use that media type and for no other.
    fn caps(self) -> (&'static CStr, Option<&'static CStr>) {
        match self {
            // FIXME 2.0: Change everything to application/x-subtitle,format=(string)XXX
            Format::MicroDvd => (c"application/x-subtitle", Some(c"microdvd")),
            Format::SubRip => (c"application/x-subtitle", Some(c"subrip")),
            Format::MpSub => (c"application/x-subtitle", Some(c"mpsub")),
            Format::SubViewer => (c"application/x-subtitle", Some(c"subviewer")),
            Format::Dks => (c"application/x-subtitle-dks", None),
            Format::Lrc => (c"application/x-subtitle-lrc", None),
            Format::Mpl2 => (c"application/x-subtitle-mpl2", None),
            Format::QtText => (c"application/x-subtitle-qttext", None),
            Format::Sami => (c"application/x-subtitle-sami", None),
            Format::TmPlayer => (c"application/x-subtitle-tmplayer", None),
            Format::WebVtt => (c"application/x-subtitle-vtt", None),
        }
    }
}

pub(crate) extern "C" fn subtitle_type_find(tf: &mut TypeFind, _private: gpointer) {
    let Some(data) = tf.peek_at_most(128) else {
        return;
    };
    let Some(format) = detect_format(data) else {
        return;
    };

    let (media, format_value) = format.caps();
    if let Some(f) = format_value {
        suggest!(tf, Prob::MAXIMUM, media, [
            (c"format", str : f),
        ]);
    } else if matches!(format, Format::WebVtt) {
        // The only format with the `parsed` field.
        suggest!(tf, Prob::MAXIMUM, media, [
            (c"parsed", bool : false),
        ]);
    } else {
        suggest!(tf, Prob::MAXIMUM, media);
    }
}

/// Convert the raw subtitle data to UTF-8, if possible.
///
/// - data with a BOM at the beginning is converted unconditionally,
/// - otherwise, when the text is not valid UTF-8 (ignoring up to 8 trailing
///   bytes of an incomplete sequence), it is converted with
///   `fallback_encoding()`.
///
/// A conversion failure leaves the original data in place.
fn subtitle_text(data: &[u8]) -> Vec<u8> {
    let mut text = data.to_vec();

    if let Some(encoding) = detect_bom_encoding(&text)
        && let Some(converted) = convert_to_utf8(&text, encoding)
    {
        text = converted;
    }

    let checked_len = text.len().saturating_sub(8);
    if std::str::from_utf8(&text[..checked_len]).is_err()
        && let Some(encoding) = fallback_encoding()
        && let Some(converted) = convert_to_utf8(&text, &encoding)
    {
        text = converted;
    }

    text
}

/// The encoding to try for text that is not valid UTF-8:
/// `GST_SUBTITLE_ENCODING`, else the locale charset, or ISO-8859-15 if the
/// locale is UTF-8.
fn fallback_encoding() -> Option<String> {
    std::env::var("GST_SUBTITLE_ENCODING")
        .ok()
        .filter(|s| !s.is_empty())
        .or_else(|| {
            let (charset, is_utf8) = locale_charset()?;
            Some(if is_utf8 {
                "ISO-8859-15".to_owned()
            } else {
                charset
            })
        })
}

/// The charset of a BOM-prefixed stream, as an iconv-compatible charset name.
fn detect_bom_encoding(data: &[u8]) -> Option<&'static str> {
    if data.starts_with(b"\xef\xbb\xbf") {
        Some("UTF-8")
    } else if data.starts_with(b"\x00\x00\xfe\xff") {
        Some("UTF-32BE")
    } else if data.starts_with(b"\xff\xfe\x00\x00") {
        Some("UTF-32LE")
    } else if data.starts_with(b"\xfe\xff") {
        Some("UTF-16BE")
    } else if data.starts_with(b"\xff\xfe") {
        Some("UTF-16LE")
    } else {
        None
    }
}

/// Detect the subtitle format of `data`.
fn detect_format(data: &[u8]) -> Option<Format> {
    let text = subtitle_text(data);

    if match_mdvd(&text) {
        return Some(Format::MicroDvd);
    }
    if match_subrip(&text) {
        return Some(Format::SubRip);
    }
    if match_dks(&text) {
        return Some(Format::Dks);
    }
    if match_webvtt(&text) {
        return Some(Format::WebVtt);
    }
    if match_mpsub(&text) {
        return Some(Format::MpSub);
    }
    if match_sami(&text) {
        return Some(Format::Sami);
    }
    if match_tmplayer(&text) {
        return Some(Format::TmPlayer);
    }
    if match_mpl2(&text) {
        return Some(Format::Mpl2);
    }
    if match_subviewer(&text) {
        return Some(Format::SubViewer);
    }
    if match_qttext(&text) {
        return Some(Format::QtText);
    }
    if match_lrc(&text) {
        return Some(Format::Lrc);
    }
    None
}

/// Consume 1..=max ASCII digits from `data`, advancing it past them. Returns
/// `false` if the first byte is not a digit.
fn take_digits(data: &mut &[u8], max: usize) -> bool {
    let n = data
        .iter()
        .take_while(|b| b.is_ascii_digit())
        .take(max)
        .count();
    *data = &data[n..];
    n > 0
}

/// Consume one or more ASCII spaces from `data`, advancing it past them.
/// Returns `false` if no space is present.
fn take_spaces(data: &mut &[u8]) -> bool {
    let n = data.iter().take_while(|b| **b == b' ').count();
    *data = &data[n..];
    n > 0
}

/// Consume `prefix` from `data`, advancing it past it. Returns `false` if
/// `data` does not start with `prefix`.
fn take_prefix(data: &mut &[u8], prefix: &[u8]) -> bool {
    let Some(rest) = data.strip_prefix(prefix) else {
        return false;
    };
    *data = rest;
    true
}

/// MicroDVD: Two `{digits}` groups back to back at the start of the stream.
///
/// ```text
/// {123}{456}Hello!
/// ```
fn match_mdvd(data: &[u8]) -> bool {
    let mut d = data;
    for _ in 0..2 {
        if !take_prefix(&mut d, b"{")
            || !take_digits(&mut d, usize::MAX)
            || !take_prefix(&mut d, b"}")
        {
            return false;
        }
    }
    true
}

/// SubRip (SRT): An index line (1-4 digits, possibly space padded) followed by a
/// `-->` time range on the next line.
///
/// ```text
/// 1
/// 00:00:01,000 --> 00:00:02,000
/// Hello!
/// ```
fn match_subrip(data: &[u8]) -> bool {
    // Leading whitespace.
    let off = data.iter().take_while(|b| b.is_ascii_whitespace()).count();
    let mut d = &data[off..];
    // 1-4 character (space padded) index.
    let n = d
        .iter()
        .take(4)
        .take_while(|b| b.is_ascii_digit() || **b == b' ')
        .count();
    if n == 0 {
        return false;
    }
    d = &d[n..];
    // Whitespace up to and including the line feed. A single space may
    // follow it, as part of the timestamp, whose hours tolerate a leading
    // space; the run is therefore cut at the feed, not past it.
    let run = d.iter().take_while(|b| b.is_ascii_whitespace()).count();
    let Some(feed) = d[..run].iter().position(|b| *b == b'\n') else {
        return false;
    };
    d = &d[feed + 1..];
    // First timestamp.
    if !match_subrip_timestamp(&mut d, 3) {
        return false;
    }
    // One or more spaces, "-->", one or more spaces.
    if !take_spaces(&mut d) || !take_prefix(&mut d, b"-->") || !take_spaces(&mut d) {
        return false;
    }
    // Second timestamp.
    match_subrip_timestamp(&mut d, 2)
}

/// Hours, minutes and seconds of 1-2 digits each (each optionally preceded
/// by a space), then a '.' or ',' fraction separator, up to two spaces and
/// 1..=ms_max fraction digits, e.g. `01:02:03,456`.
fn match_subrip_timestamp(data: &mut &[u8], ms_max: usize) -> bool {
    // Hours and minutes.
    for _ in 0..2 {
        take_prefix(data, b" ");
        if !take_digits(data, 2) || !take_prefix(data, b":") {
            return false;
        }
    }
    // Seconds and the fraction separator.
    take_prefix(data, b" ");
    if !take_digits(data, 2) {
        return false;
    }
    if !take_prefix(data, b",") && !take_prefix(data, b".") {
        return false;
    }
    // Up to two spaces before the fraction.
    take_prefix(data, b" ");
    take_prefix(data, b" ");
    take_digits(data, ms_max)
}

/// DKS subtitles: A `[H:MM:SS]` timestamp at the start of the stream.
///
/// ```text
/// [00:00:17]Hello,
/// [00:01:00]World!
/// ```
fn match_dks(data: &[u8]) -> bool {
    let mut d = data;
    if !take_prefix(&mut d, b"[") {
        return false;
    }
    for _ in 0..2 {
        if !take_digits(&mut d, usize::MAX) || !take_prefix(&mut d, b":") {
            return false;
        }
    }
    take_digits(&mut d, usize::MAX) && take_prefix(&mut d, b"]")
}

/// WebVTT: The `WEBVTT` header line: `WEBVTT` followed by a space, tab, LF or CR,
/// then style block and SRT-style cues but without the index.
///
/// ```text
/// WEBVTT
/// 00:00:01,000 --> 00:00:02,000
/// Hello!
/// ```
fn match_webvtt(data: &[u8]) -> bool {
    data.strip_prefix(b"WEBVTT")
        .is_some_and(|rest| matches!(rest.first(), Some(b' ' | b'\t' | b'\n' | b'\r')))
}

/// MPSub (MPlayer): Key=Value pairs header, then cues with two numbers, a newline and the text.
/// FORMAT=TIME header is mandatory.
///
/// ```text
/// TITLE=Hello World
/// FORMAT=TIME
///
/// 17 5
/// Hello,
///
/// 0 5
/// World!
/// ```
fn match_mpsub(data: &[u8]) -> bool {
    data.windows(11).any(|w| w == b"FORMAT=TIME")
}

/// SAMI: `<SAMI>` or `<sami>` anywhere in the peeked text.
fn match_sami(data: &[u8]) -> bool {
    data.windows(6).any(|w| w == b"<SAMI>" || w == b"<sami>")
}

/// TMPlayer: A `0:MM:SS` or `00:MM:SS` timestamp at the start of each line followed by a separator and the text.
///
/// The separator after the timestamp must be `:`, `=`, or a comma-separated
/// subtitle index followed by `=`, e.g. `00:00:10,1=`.
///
/// ```text
/// 00:01:02:Hello,
/// 00:01:30:World!
/// ```
fn match_tmplayer(data: &[u8]) -> bool {
    let mut d = data;
    if !take_prefix(&mut d, b"00:") && !take_prefix(&mut d, b"0:") {
        return false;
    }
    // MM:SS
    if !(take_digits(&mut d, 2) && take_prefix(&mut d, b":") && take_digits(&mut d, 2)) {
        return false;
    }
    // Separator: ':' or '=' or ',digits='
    if take_prefix(&mut d, b":") || take_prefix(&mut d, b"=") {
        return true;
    }
    take_prefix(&mut d, b",") && take_digits(&mut d, usize::MAX) && take_prefix(&mut d, b"=")
}

/// MPL2: Two consecutive `[digits]` groups, e.g. `[123][456]` per line, followed by the
/// text and | for newlines.
///
/// ```text
/// [10][50]Hello,|World!
/// ```
fn match_mpl2(data: &[u8]) -> bool {
    let mut d = data;
    for _ in 0..2 {
        if !take_prefix(&mut d, b"[")
            || !take_digits(&mut d, usize::MAX)
            || !take_prefix(&mut d, b"]")
        {
            return false;
        }
    }

    true
}

/// SubViewer: `[INFORMATION]` anywhere in the peeked text.
///
/// ```text
/// [INFORMATION]
/// [TITLE] Hello World!
/// 00:00:00.10,00:00:10.00
/// Hello,
///
/// 00:00:12.00,00:00:20.00
/// World!
/// ```
fn match_subviewer(data: &[u8]) -> bool {
    data.windows(13).any(|w| w == b"[INFORMATION]")
}

/// QuickTime Text: `{QTtext}` somewhere in the beginning.
///
/// ```text
/// {QTtext}{font:Arial}{plain}
/// [0:00:00.10]
/// Hello,
/// [0:00:06.20]
/// World!
/// ```
fn match_qttext(data: &[u8]) -> bool {
    data.windows(8).any(|w| w == b"{QTtext}")
}

/// LRC: The first byte is '[' and every line but the last is an LRC line, e.g. a
/// `[03:45.67]` timestamp line.
///
/// ```text
/// [ti:Hello World!]
/// [00:01.00]Hello,
/// [00:10.00]World!
/// ```
fn match_lrc(data: &[u8]) -> bool {
    if !data.starts_with(b"[") {
        return false;
    }
    // Every line but the last must be an LRC line.
    let mut lines = data.split(|&b| b == b'\n');
    lines.next_back();
    lines.all(match_lrc_line)
}

/// An LRC timestamp `[M:SS.digits]` with 1-3 fraction digits, e.g. `[03:45.67]`, or any other
/// non-empty line starting with `[` and ending in `]` that contains ':'.
fn match_lrc_line(line: &[u8]) -> bool {
    // LRC timestamp [M:SS.digits] with 1-3 fraction digits.
    let mut d = line;
    if take_prefix(&mut d, b"[")
        && take_digits(&mut d, usize::MAX)
        && take_prefix(&mut d, b":")
        && take_digits(&mut d, 2)
        && take_prefix(&mut d, b".")
        && take_digits(&mut d, 3)
        && take_prefix(&mut d, b"]")
    {
        return true;
    }

    line.starts_with(b"[") && line.last() == Some(&b']') && line.contains(&b':')
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The index line and the timestamp are separated by the feed and at
    /// most one space; a deeper indent is not that.
    #[test]
    fn subrip_allows_one_space_before_the_timestamp() {
        assert!(match_subrip(b"1\n 00:00:01,000 --> 00:00:02,000\nOne\n"));
        assert!(!match_subrip(b"1\n  00:00:01,000 --> 00:00:02,000\nOne\n"));
        assert!(!match_subrip(b"1\n\t00:00:01,000 --> 00:00:02,000\nOne\n"));
    }

    const CASES: &[(Format, &[&[u8]])] = &[
        (
            Format::MicroDvd,
            &[b"{1}{1}12.500\n{100}{200}- Hi, Eddie.|- Hiya, Scotty.\n", b"{123}{456}Hello world\n"],
        ),
        (
            Format::SubRip,
            &[
                b"1\n00:00:01,000 --> 00:00:02,000\nOne\n\n",
                b" 2\n00:00:01,000 --> 00:00:04,000\nHello world\n",
                b"1\n 00:00:01,000 --> 00:00:02,000\nOne\n\n",
            ],
        ),
        (
            Format::Dks,
            &[
                b"[00:00:01]Hello\n",
                b"[00:00:07]THERE IS A PLACE ON EARTH WHERE IT[br]IS STILL THE MORNING OF LIFE...\n[00:00:12]\n",
            ],
        ),
        (
            Format::WebVtt,
            &[
                b"WEBVTT\n\n00:00:01.000 --> 00:00:04.000\nHello\n",
                b"WEBVTT \n",
                b"\xef\xbb\xbfWEBVTT\n",
                b"\x00\x00\xfe\xff\x00\x00\x00W\x00\x00\x00E\x00\x00\x00B\x00\x00\x00V\x00\x00\x00T\x00\x00\x00T\x00\x00\x00\n",
            ],
        ),
        (Format::MpSub, &[b"FORMAT=TIME\n"]),
        (
            Format::Sami,
            &[
                b"<SAMI>\n<HEAD>\n    <TITLE>Subtitle</TITLE>\n",
                b"blah<sami>blah",
            ],
        ),
        (
            Format::TmPlayer,
            &[
                b"00:00:10,1=This is the Earth at a time\n00:00:10,2=when the dinosaurs roamed...\n00:00:13,1=\n",
                b"0:00:01:Hello\n",
            ],
        ),
        (
            Format::Mpl2,
            &[
                b"[123][456] This is the Earth at a time|when the dinosaurs roamed\n",
                b"[01][02]Hello\n",
            ],
        ),
        (
            Format::SubViewer,
            &[
                b"[INFORMATION]\n[TITLE]xxxxxxxxxx\n[AUTHOR]xxxxxxxx\n[SOURCE]xxxxxxxxxxxxxxxx\n[FILEPATH]\n[DELAY]0\n[COMMENT]\n[END INFORMATION]\n[SUBTITLE]\n[COLF]&HFFFFFF,[STYLE]bd,[SIZE]18,[FONT]Arial\n00:00:41.00,00:00:44.40\nThe Age of Gods was closing.\nEternity had come to an end.\n\n",
                b"blah [INFORMATION] blah",
            ],
        ),
        (Format::QtText, &[b"{QTtext}Hello\n"]),
        (
            Format::Lrc,
            &[
                b"[ar:123]\n[ti:Title]\n[al:Album]\n[00:02.23]Line 1\n",
                b"[00:01.00]Hello\n[00:02.50]World\n",
            ],
        ),
    ];

    #[test]
    fn detect() {
        for (format, s) in CASES {
            for input in *s {
                assert_eq!(detect_format(input), Some(*format), "input: {input:?}");
            }
        }
    }
}
