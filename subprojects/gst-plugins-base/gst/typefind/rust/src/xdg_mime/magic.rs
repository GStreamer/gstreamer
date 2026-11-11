// GStreamer
//
// Copyright (c) Emmanuele Bassi
//
// Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
// licensed under the Apache License, Version 2.0. Upstream ships no per-file
// headers; LICENSE-APACHE in this directory is the upstream notice.
// Modifications are licensed under the same terms.

//! Content rules from the shared MIME database (`magic` file).
//!
//! Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
//! Copyright (c) Emmanuele Bassi, Apache-2.0. The `nom`-generated parser is
//! replaced by a std-only cursor parser over the same grammar.
//! See the provenance note in `super`.

use std::fmt;
use std::fs::File;
use std::io::Read;
use std::path::Path;

use super::is_mime_type;

/// Byte cursor over the magic file. Every accessor returns `None` at or
/// beyond the end of the buffer; a failed read aborts parsing of the current
/// entry.
struct Cursor<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Cursor<'a> {
    fn new(bytes: &'a [u8]) -> Cursor<'a> {
        Cursor { bytes, pos: 0 }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn starts_with(&self, prefix: &[u8]) -> bool {
        self.bytes[self.pos..].starts_with(prefix)
    }

    /// Consume and return the next `n` bytes.
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        let end = self.pos.checked_add(n)?;
        let slice = self.bytes.get(self.pos..end)?;
        self.pos = end;
        Some(slice)
    }

    /// Consume and return the bytes up to (not including) `marker`.
    fn take_until(&mut self, marker: u8) -> Option<&'a [u8]> {
        let offset = self.bytes[self.pos..].iter().position(|&b| b == marker)?;
        self.take(offset)
    }

    fn eat(&mut self, byte: u8) -> Option<()> {
        if self.peek()? == byte {
            self.pos += 1;
            Some(())
        } else {
            None
        }
    }

    /// Consume and return the longest run of bytes satisfying `pred`.
    fn take_while(&mut self, pred: impl Fn(u8) -> bool) -> &'a [u8] {
        let start = self.pos;
        while self.peek().is_some_and(&pred) {
            self.pos += 1;
        }
        &self.bytes[start..self.pos]
    }

    /// `u32` from the consumed bytes, decimal, defaulting to `default` on
    /// non-numeric or non-UTF-8 content (upstream `buf_to_u32` parity: the
    /// file's decimal fields are parsed leniently).
    fn decimal(default: u32, bytes: &[u8]) -> u32 {
        std::str::from_utf8(bytes)
            .ok()
            .and_then(|s| s.parse::<u32>().ok())
            .unwrap_or(default)
    }
}

/// One `>offset=value` test. Rules of an entry form a flat list describing a
/// tree; `indent` is the depth of the rule in that tree.
#[derive(Clone, Debug, PartialEq)]
struct MagicRule {
    indent: u32,
    start_offset: u32,
    value: Vec<u8>,
    mask: Option<Vec<u8>>,
    range_length: u32,
}

impl MagicRule {
    fn matches_data(&self, data: &[u8]) -> bool {
        debug_assert!(
            self.mask
                .as_ref()
                .is_none_or(|mask| mask.len() == self.value.len())
        );

        let value_len = self.value.len();
        let mut windows = data
            .windows(value_len)
            .skip(self.start_offset as usize)
            .take(self.range_length as usize);

        match &self.mask {
            Some(mask) => windows.any(|window| {
                window
                    .iter()
                    .zip(mask)
                    .zip(self.value.iter())
                    .all(|((d, m), v)| d & m == *v)
            }),
            None => windows.any(|window| window == &self.value[..]),
        }
    }

    /// Number of data bytes needed to evaluate this rule (upstream parity:
    /// the range is counted even though only offsets up to
    /// `start_offset + range_length` are actually read).
    fn extent(&self) -> usize {
        self.value.len() + self.start_offset as usize + self.range_length as usize
    }
}

/// magic_rule =
/// [ <indent> ] '>' <start-offset> '=' <u16be value-length> <value>
/// [ '&' <mask of value-length bytes> ] [ '~' (0|1|2|4) ] [ '+' <digits> ] '\n'
fn magic_rule(cur: &mut Cursor) -> Option<MagicRule> {
    // A rule starts with an indent digit or '>'; anything else ends the entry.
    let first = cur.peek()?;
    if !first.is_ascii_digit() && first != b'>' {
        return None;
    }

    let indent = Cursor::decimal(0, cur.take_until(b'>')?);
    cur.eat(b'>')?;
    let start_offset = Cursor::decimal(0, cur.take_until(b'=')?);
    cur.eat(b'=')?;

    let value_len = u16::from_be_bytes(cur.take(2)?.try_into().ok()?) as usize;
    // A zero-length value would trivially match everywhere (and panics
    // `windows(0)` at match time); treat it as a malformed file.
    if value_len == 0 {
        return None;
    }
    let value = cur.take(value_len)?;

    let mask = match cur.peek() {
        Some(b'&') => {
            cur.pos += 1;
            Some(cur.take(value_len)?)
        }
        _ => None,
    };

    // Upstream parity: a word size is grammar to consume, not semantics to
    // apply. The crate this derives from parses and stores it but matches on
    // raw bytes, and every `~` entry in the shipped databases spells a value
    // exactly as wide as its word, where the two readings agree.
    if cur.peek() == Some(b'~') {
        cur.pos += 1;
        match cur.peek() {
            Some(b'0' | b'1' | b'2' | b'4') => cur.pos += 1,
            _ => return None,
        }
    }

    // Upstream parity: the range takes hex-looking digits but parses them
    // decimally, defaulting to 1. The `magic` files on disk use decimal.
    let mut range_digits: &[u8] = &[];
    if cur.peek() == Some(b'+') {
        cur.pos += 1;
        range_digits = cur.take_while(|b| b.is_ascii_hexdigit());
    }
    let range_length = Cursor::decimal(1, range_digits);

    cur.eat(b'\n')?;

    Some(MagicRule {
        indent,
        start_offset,
        value: value.to_vec(),
        mask: mask.map(|m| m.to_vec()),
        range_length,
    })
}

#[derive(Clone, PartialEq)]
pub(super) struct MagicEntry {
    mime_type: String,
    priority: u32,
    rules: Vec<MagicRule>,
}

impl fmt::Debug for MagicEntry {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "MIME type: {:?} (priority: {:?}):\nrules:\n{:?}",
            self.mime_type, self.priority, self.rules
        )
    }
}

impl MagicEntry {
    fn matches(&self, data: &[u8]) -> Option<&str> {
        let mut current_level = 0;

        let mut iter = self.rules.iter().peekable();
        while let Some(rule) = iter.next() {
            // The rules are a flat list that represent a tree; the "indent"
            // is the depth of the rule in the tree.
            if rule.indent == current_level && rule.matches_data(data) {
                match iter.peek() {
                    // Next rule at the same or shallower depth: this match
                    // completes the entry.
                    Some(next) if next.indent <= current_level => {
                        return Some(&self.mime_type);
                    }
                    Some(_) => current_level += 1,
                    // Matched the last rule.
                    None => return Some(&self.mime_type),
                }
            }
        }

        None
    }

    fn max_extent(&self) -> usize {
        self.rules.iter().map(MagicRule::extent).max().unwrap_or(0)
    }
}

/// magic_header = '[' <priority> ':' <mime-type> ']' '\n'
fn magic_header<'a>(cur: &mut Cursor<'a>) -> Option<(u32, &'a str)> {
    cur.eat(b'[')?;
    let priority = Cursor::decimal(0, cur.take_until(b':')?);
    cur.eat(b':')?;
    let mime_bytes = cur.take_until(b'\n')?;
    let mime_str = std::str::from_utf8(mime_bytes).ok()?;
    let (mime_type, rest) = mime_str.split_once(']')?;
    if !rest.is_empty() || !is_mime_type(mime_type) {
        return None;
    }
    cur.eat(b'\n')?;
    Some((priority, mime_type))
}

/// magic_entry = <magic_header> <magic_rule>+
fn magic_entry(cur: &mut Cursor) -> Option<MagicEntry> {
    let (priority, mime_type) = magic_header(cur)?;

    let mut rules = Vec::new();
    while let Some(rule) = magic_rule(cur) {
        rules.push(rule);
    }
    if rules.is_empty() {
        return None;
    }

    Some(MagicEntry {
        mime_type: mime_type.to_string(),
        priority,
        rules,
    })
}

/// `file = "MIME-Magic\0\n" magic_entry*` — parsing stops at the first
/// unparseable entry; whatever was read before it is kept.
fn parse_entries(mut bytes: &[u8]) -> Vec<MagicEntry> {
    const HEADER: &[u8] = b"MIME-Magic\0\n";
    if !bytes.starts_with(HEADER) {
        return Vec::new();
    }
    bytes = &bytes[HEADER.len()..];

    let mut cur = Cursor::new(bytes);
    let mut entries = Vec::new();
    while cur.peek() == Some(b'[') {
        match magic_entry(&mut cur) {
            Some(entry) => entries.push(entry),
            None => break,
        }
    }
    // Highest priority first, file order kept on ties: the lookup returns the
    // first match, which is then the answer the unsorted scan picked.
    sort_by_priority(&mut entries);
    entries
}

/// Sort entries by descending priority, keeping the current order on ties.
/// The database is the concatenation of one `magic` file per XDG data
/// directory, so this runs again over the combined list.
pub(super) fn sort_by_priority(entries: &mut [MagicEntry]) {
    entries.sort_by_key(|entry| std::cmp::Reverse(entry.priority));
}

pub(super) fn lookup_data(entries: &[MagicEntry], data: &[u8]) -> Option<(String, u32)> {
    // The entries are sorted by descending priority, file order kept on ties,
    // so the first match is the highest-priority one.
    for entry in entries {
        if let Some(mime) = entry.matches(data) {
            return Some((mime.to_string(), entry.priority));
        }
    }
    None
}

/// Largest data extent any rule may read, used to cap sniffed input.
pub(super) fn max_extents(entries: &[MagicEntry]) -> usize {
    entries
        .iter()
        .map(MagicEntry::max_extent)
        .max()
        .unwrap_or(0)
}

pub(super) fn read_magic_from_dir(dir: &Path) -> Vec<MagicEntry> {
    let Ok(mut file) = File::open(dir.join("magic")) else {
        return Vec::new();
    };
    let mut buf = Vec::new();
    if file.read_to_end(&mut buf).is_err() {
        return Vec::new();
    }
    parse_entries(&buf)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// One `\n`-terminated rule line as raw bytes (`rest` covers the
    /// optional `&mask`, `~word` and `+range` suffixes).
    fn rule_bytes(indent: u32, offset: u32, value: &[u8], rest: &[u8]) -> Vec<u8> {
        let mut bytes = format!("{indent}>{offset}=").into_bytes();
        bytes.extend_from_slice(&(value.len() as u16).to_be_bytes());
        bytes.extend_from_slice(value);
        bytes.extend_from_slice(rest);
        bytes.extend_from_slice(b"\n");
        bytes
    }

    /// A complete magic file: `mime_type` header followed by the rules.
    fn parse(mime_type: &str, rules: &[Vec<u8>]) -> Vec<MagicEntry> {
        let mut buf = b"MIME-Magic\0\n".to_vec();
        buf.extend_from_slice(format!("[10:{mime_type}]\n").as_bytes());
        for rule in rules {
            buf.extend_from_slice(rule);
        }
        parse_entries(&buf)
    }

    /// The lookup answer is the highest-priority match, not the first in
    /// file order; a tie keeps the earlier entry.
    #[test]
    fn the_highest_priority_entry_wins() {
        let build = |specs: &[(&str, u32)]| {
            let mut buf = b"MIME-Magic\0\n".to_vec();
            for (mime, prio) in specs {
                buf.extend_from_slice(format!("[{prio}:{mime}]\n").as_bytes());
                buf.extend_from_slice(&rule_bytes(0, 0, b"AB", b""));
            }
            parse_entries(&buf)
        };
        let entries = build(&[("text/low", 40), ("text/high", 60)]);
        let (mime, prio) = lookup_data(&entries, b"AB").unwrap();
        assert_eq!((mime.as_str(), prio), ("text/high", 60));
        let tied = build(&[("text/first", 60), ("text/second", 60)]);
        let (mime, _) = lookup_data(&tied, b"AB").unwrap();
        assert_eq!(mime, "text/first");
    }

    #[test]
    fn parses_simple_entry() {
        let entries = parse("text/plain", &[rule_bytes(0, 0, b"BEGIN", b"")]);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].mime_type, "text/plain");
        assert_eq!(entries[0].rules.len(), 1);
        assert_eq!(entries[0].rules[0].range_length, 1);
        assert_eq!(entries[0].rules[0].indent, 0);
    }

    #[test]
    fn parses_options() {
        // mask, word size, range
        let entries = parse(
            "application/x-test",
            &[rule_bytes(0, 4, b"AB", b"&\xff\xff~2+10")],
        );
        assert_eq!(entries.len(), 1);
        let rule = &entries[0].rules[0];
        assert_eq!(rule.mask.as_deref(), Some(&[0xffu8, 0xff][..]));
        assert_eq!(rule.start_offset, 4);
        assert_eq!(rule.range_length, 10);
    }

    #[test]
    fn nested_rules_require_child_match() {
        let entries = parse(
            "application/x-foo",
            &[rule_bytes(0, 0, b"FOO", b""), rule_bytes(1, 8, b"BAR", b"")],
        );
        assert_eq!(entries.len(), 1);

        // Root matches and child matches.
        assert_eq!(
            entries[0].matches(b"FOO-----BAR"),
            Some("application/x-foo")
        );
        // Root matches but child does not.
        assert_eq!(entries[0].matches(b"FOO-----XXX"), None);
    }

    #[test]
    fn masked_match() {
        let entries = parse(
            "application/x-mask",
            &[rule_bytes(0, 0, b"Ab", b"&\xdf\xff")],
        );
        // 'a' & 0xdf == 'A', so the masked rule matches both cases.
        assert_eq!(entries[0].matches(b"ab..."), Some("application/x-mask"));
        assert_eq!(entries[0].matches(b"bA..."), None);
    }

    #[test]
    fn truncates_at_malformed_entry() {
        // A second, broken header after a good entry keeps the good one.
        let mut buf = b"MIME-Magic\0\n[10:text/plain]\n".to_vec();
        buf.extend_from_slice(&rule_bytes(0, 0, b"OK", b""));
        buf.extend_from_slice(b"[20:bad-header-no-close-bracket\n");
        assert_eq!(parse_entries(&buf).len(), 1);

        // Entry without rules is dropped.
        assert!(parse_entries(b"MIME-Magic\0\n[10:text/plain]\n").is_empty());
        // Junk header on the file.
        assert!(parse_entries(b"not-magic").is_empty());
    }

    #[test]
    fn zero_length_value_is_malformed() {
        // value_len == 0 would match trivially; upstream xdg-mime panics
        // (slice::windows(0)) on such files, we reject the entry instead.
        let mut buf = b"MIME-Magic\0\n[10:text/plain]\n0>0=".to_vec();
        buf.extend_from_slice(&0u16.to_be_bytes());
        buf.extend_from_slice(b"\n");
        assert!(parse_entries(&buf).is_empty());
    }

    #[test]
    fn range_offsets_scan_window() {
        // Value may appear anywhere in [start, start + range_length).
        let entries = parse("application/x-range", &[rule_bytes(0, 0, b"XZ", b"+4")]);
        assert_eq!(entries[0].matches(b"abXZ"), Some("application/x-range"));
        assert_eq!(entries[0].matches(b"XZ"), Some("application/x-range"));
        assert_eq!(entries[0].matches(b"abXY"), None);
    }

    /// The old, unoptimized matching: every window compared, every entry
    /// scanned, best priority kept.
    fn reference_lookup(entries: &[MagicEntry], data: &[u8]) -> Option<(String, u32)> {
        fn rule_matches(rule: &MagicRule, data: &[u8]) -> bool {
            let value_len = rule.value.len();
            let mut windows = data
                .windows(value_len)
                .skip(rule.start_offset as usize)
                .take(rule.range_length as usize);
            match &rule.mask {
                Some(mask) => windows.any(|window| {
                    window
                        .iter()
                        .zip(mask)
                        .zip(rule.value.iter())
                        .all(|((d, m), v)| d & m == *v)
                }),
                None => windows.any(|window| window == &rule.value[..]),
            }
        }
        fn entry_matches<'a>(entry: &'a MagicEntry, data: &[u8]) -> Option<&'a str> {
            let mut current_level = 0;
            let mut iter = entry.rules.iter().peekable();
            while let Some(rule) = iter.next() {
                if rule.indent == current_level && rule_matches(rule, data) {
                    match iter.peek() {
                        Some(next) if next.indent <= current_level => {
                            return Some(&entry.mime_type);
                        }
                        Some(_) => current_level += 1,
                        None => return Some(&entry.mime_type),
                    }
                }
            }
            None
        }
        let mut best: Option<(String, u32)> = None;
        for entry in entries {
            if let Some(mime) = entry_matches(entry, data)
                && best
                    .as_ref()
                    .is_none_or(|(_, priority)| entry.priority > *priority)
            {
                best = Some((mime.to_string(), entry.priority));
            }
        }
        best
    }

    #[test]
    #[ignore = "differential test against the system shared MIME database"]
    fn optimized_lookup_matches_reference() {
        let entries = read_magic_from_dir(std::path::Path::new("/usr/share/mime"));
        assert!(!entries.is_empty(), "no magic rules parsed");

        // Deterministic pseudo-random buffers, plus real file prefixes.
        let mut seed = 0x2545_f491_4f6c_dd1du64;
        let mut next = move || {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            seed
        };
        let mut buffers: Vec<Vec<u8>> = Vec::new();
        for len in [0, 1, 2, 3, 5, 8, 16, 64, 256, 1024, 4096, 16384] {
            for _ in 0..20 {
                let mut buf = vec![0u8; len];
                for b in &mut buf {
                    *b = next() as u8;
                }
                buffers.push(buf);
            }
        }
        for file in [
            "/usr/share/mime/magic",
            "/usr/share/mime/globs2",
            "/etc/passwd",
            "/usr/bin/ls",
        ] {
            if let Ok(bytes) = std::fs::read(file) {
                for len in [16, 256, 4096, 16384] {
                    buffers.push(bytes[..bytes.len().min(len)].to_vec());
                }
            }
        }

        for data in &buffers {
            assert_eq!(
                lookup_data(&entries, data),
                reference_lookup(&entries, data),
                "mismatch on {} bytes",
                data.len()
            );
        }
    }
}
