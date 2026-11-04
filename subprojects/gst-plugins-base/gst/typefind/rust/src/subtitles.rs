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

// SSA/ASS subtitles - application/x-ssa and application/x-ass
//
// The dialect opens its Script Info section and names its version in
// it; either fact alone is a claim, and the version settles which of
// the two media types answers.

/// How far the search for the version reaches.
const SSA_WINDOW: usize = 8192;

/// How far back from the window end a cut-through character may reach.
/// No UTF-8 sequence is longer than four octets; the tolerance is the
/// spec's, six, and it is spent only on sequences that could continue.
const UTF8_PARTIAL_MAX: usize = 6;

pub(crate) extern "C" fn ssa_type_find(tf: &mut TypeFind, _private: gpointer) {
    if let Some(caps) = ssa_probe(tf) {
        suggest!(tf, Prob::MAXIMUM, caps, [
            (c"parsed", bool : false),
        ]);
    }
}

fn ssa_probe(tf: &mut TypeFind) -> Option<&'static CStr> {
    // A file shorter than the search window is judged on what it holds.
    let data = tf.peek_at_most(SSA_WINDOW)?;
    ssa_detect(data)
}

fn ssa_detect(data: &[u8]) -> Option<&'static CStr> {
    let body = data
        .strip_prefix(b"\xEF\xBB\xBF".as_slice())
        .unwrap_or(data);
    if !body.starts_with(b"[Script Info]") {
        return None;
    }
    // The text must read as UTF-8, with one tolerance: a trailing
    // sequence the window could have cut mid-character. An error
    // without a length is the final incomplete sequence, and anything
    // wrong before it rules.
    if let Err(e) = std::str::from_utf8(body)
        && !(e.error_len().is_none() && body.len() - e.valid_up_to() <= UTF8_PARTIAL_MAX)
    {
        return None;
    }
    if let Some(mut value) = find_key(data, b"ScriptType=") {
        value = strip_spaces(value);
        if let Some(rest) = value
            .strip_prefix(b"v".as_slice())
            .or_else(|| value.strip_prefix(b"V".as_slice()))
        {
            value = rest;
        }
        if let Some((number, read)) = decimal(value) {
            if number == 4.0 && value[read..].starts_with(b"+") {
                return Some(c"application/x-ass");
            }
            if (1.0..=4.0).contains(&number) {
                return Some(c"application/x-ssa");
            }
            // A version out of the claimed range says nothing more than
            // the section names below do.
        }
    }
    // An unparseable version still names its styles section in both
    // cases of the v, and the case of the letter is the claim.
    for (section, caps) in [
        (b"[v4+ Styles]".as_slice(), c"application/x-ass"),
        (b"[V4+ Styles]".as_slice(), c"application/x-ass"),
        (b"[v4 Styles]".as_slice(), c"application/x-ssa"),
        (b"[V4 Styles]".as_slice(), c"application/x-ssa"),
    ] {
        if data.windows(section.len()).any(|w| w == section) {
            return Some(caps);
        }
    }
    None
}

/// The bytes behind the first occurrence of `key`, or None when the key
/// is beyond the window.
fn find_key<'a>(data: &'a [u8], key: &[u8]) -> Option<&'a [u8]> {
    let at = data.windows(key.len()).position(|w| w == key)?;
    Some(&data[at + key.len()..])
}

fn strip_spaces(mut value: &[u8]) -> &[u8] {
    while let Some(rest) = value.strip_prefix(b" ".as_slice()) {
        value = rest;
    }
    value
}

/// The decimal at the head of `value` and the octets it spans, parsed
/// as a prefix: `4.00 rev 1` is four, and the span ends where the
/// number does. A run of digits without a fractional part is whole.
fn decimal(value: &[u8]) -> Option<(f64, usize)> {
    let mut at = 0;
    let mut whole: f64 = 0.0;
    while at < value.len() && value[at].is_ascii_digit() {
        whole = whole * 10.0 + f64::from(value[at] - b'0');
        at += 1;
    }
    if at == 0 {
        return None;
    }
    let mut scale = 0.1;
    if at < value.len() && value[at] == b'.' {
        at += 1;
        while at < value.len() && value[at].is_ascii_digit() {
            whole += f64::from(value[at] - b'0') * scale;
            scale *= 0.1;
            at += 1;
        }
    }
    Some((whole, at))
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
    use crate::helpers::probe_engine::probe;

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

    // ssa / ass

    fn ssa_doc(head: &[u8], version: &[u8], tail: &[u8]) -> Vec<u8> {
        let mut data = head.to_vec();
        data.extend_from_slice(b"[Script Info]\nTitle: fixture\n");
        if !version.is_empty() {
            data.extend_from_slice(b"ScriptType=");
            data.extend_from_slice(version);
            data.push(b'\n');
        }
        data.extend_from_slice(tail);
        data
    }

    /// The version claims the dialect: 4.0 with a plus is the ass
    /// script format, the whole span one to four is substation alpha,
    /// and a plain 4.00 sits on the ssa side of the border.
    #[test]
    fn ssa_versions_name_their_dialect() {
        for version in [b"v4.00+".as_slice(), b"4.0+", b"  V4.00+", b"v4+"] {
            assert_eq!(
                ssa_detect(&ssa_doc(b"", version, b"")),
                Some(c"application/x-ass"),
                "version: {version:?}"
            );
        }
        for version in [b"v4.00".as_slice(), b"v1.00", b"2.00", b"3.00", b"4.0"] {
            assert_eq!(
                ssa_detect(&ssa_doc(b"", version, b"")),
                Some(c"application/x-ssa"),
                "version: {version:?}"
            );
        }
        // Out of the span, just below its lower edge included: the
        // version says nothing.
        assert_eq!(ssa_detect(&ssa_doc(b"", b"v5.00", b"")), None);
        assert_eq!(ssa_detect(&ssa_doc(b"", b"0.99", b"")), None);
        assert_eq!(ssa_detect(&ssa_doc(b"", b"0.9", b"")), None);
        assert_eq!(ssa_detect(&ssa_doc(b"", b"zero", b"")), None);
    }

    /// An unreadable version falls through to the styles section, whose
    /// leading letter claims the dialect in both cases.
    #[test]
    fn ssa_sections_settle_the_fall_through() {
        for (section, dialect) in [
            (b"[v4+ Styles]".as_slice(), c"application/x-ass"),
            (b"[V4+ Styles]".as_slice(), c"application/x-ass"),
            (b"[v4 Styles]".as_slice(), c"application/x-ssa"),
            (b"[V4 Styles]".as_slice(), c"application/x-ssa"),
        ] {
            let tail = [section, b"\n[Events]\n"].concat();
            assert_eq!(ssa_detect(&ssa_doc(b"", b"", &tail)), Some(dialect));
            // A version the reading cannot settle lands here too.
            assert_eq!(ssa_detect(&ssa_doc(b"", b"zero", &tail)), Some(dialect));
        }
        // The section names are read whole: a plus in the plain name is
        // not decoration, in either case of the letter.
        assert_eq!(ssa_detect(&ssa_doc(b"", b"", b"[v4++ Styles]\n")), None);
        assert_eq!(ssa_detect(&ssa_doc(b"", b"", b"[V4++ Styles]\n")), None);
        assert_eq!(ssa_detect(&ssa_doc(b"", b"", b"[v4  Styles]\n")), None);
    }

    /// The opening names the file, the text has to read as UTF-8, and a
    /// window cut through a character is tolerated.
    #[test]
    fn ssa_opening_and_textuality() {
        assert_eq!(
            ssa_detect(&ssa_doc(b"\xEF\xBB\xBF", b"v4.00+", b"")),
            Some(c"application/x-ass")
        );
        // The header alone claims nothing, and its name is exact in
        // its case.
        assert_eq!(ssa_detect(&ssa_doc(b"", b"v4.00", b"")[..13]), None);
        assert_eq!(ssa_detect(b"[script info]\nScriptType=v4.00+\n"), None);
        // Ill-formed UTF-8 dismisses the file, version or not.
        assert_eq!(
            ssa_detect(&ssa_doc(
                b"",
                b"v4.00+",
                b"[\xFF]\npadding padding padding\n"
            )),
            None
        );
        // A octet that opens no sequence is not a window cut, even
        // alone at the end.
        assert_eq!(ssa_detect(&ssa_doc(b"", b"v4.00+", b"tail\xFF")), None);
        // A sequence the window could have cut mid-way is tolerated,
        // however near the end its opening sits: the emoji arrives as
        // its first two octets and nothing else follows.
        let mut tail = "\u{1F600}".as_bytes().to_vec();
        tail.truncate(2);
        assert_eq!(
            ssa_detect(&ssa_doc(b"", b"v4.00+", &tail)),
            Some(c"application/x-ass")
        );
        // Text whole up to the cut, a whole character between: the cut
        // reaches back past it into the window's last six octets.
        let mut tail = "é".as_bytes().to_vec();
        tail.extend_from_slice(&"☃".as_bytes()[..2]);
        assert_eq!(
            ssa_detect(&ssa_doc(b"", b"v4.00+", &tail)),
            Some(c"application/x-ass")
        );
        // A suggestion at full confidence with the parsed field, the
        // whole typefinding function answering. The caps string is
        // exactly what gst_caps_to_string spells: serialization pinned.
        assert_eq!(
            crate::helpers::probe_engine::probe_suggestions(&ssa_doc(b"", b"v4.00+", b""), |tf| {
                ssa_type_find(tf, std::ptr::null_mut())
            }),
            [(100, "application/x-ass, parsed=(boolean)false".to_string())]
        );
    }

    /// The version is searched as far as the window reaches, and no
    /// further: the same document reads differently by one octet of
    /// padding.
    #[test]
    fn ssa_version_search_stops_at_the_window() {
        let build = |key_at: usize| -> Vec<u8> {
            let mut data = b"[Script Info]\n".to_vec();
            data.resize(key_at, b';');
            data.extend_from_slice(b"ScriptType=v4.00+\n");
            data
        };
        // The key ends six octets shy of the window; its value fills the rest.
        let inside = build(SSA_WINDOW - b"ScriptType=".len() - 6);
        assert_eq!(probe(&inside, ssa_probe), Some(c"application/x-ass"));
        // One octet further the key straddles the window end and is no
        // longer found in it.
        let outside = build(SSA_WINDOW - b"ScriptType=".len() + 1);
        assert_eq!(probe(&outside, ssa_probe), None);
    }

    #[test]
    fn ssa_decimal_reads_its_prefix() {
        assert_eq!(decimal(b"4.00 rev 1"), Some((4.0, 4)));
        assert_eq!(decimal(b"1"), Some((1.0, 1)));
        assert_eq!(decimal(b"14.5x"), Some((14.5, 4)));
        assert_eq!(decimal(b".5"), None);
        assert_eq!(decimal(b""), None);
    }
}
