// GStreamer
//
// Copyright (c) Emmanuele Bassi
//
// Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
// licensed under the Apache License, Version 2.0. Upstream ships no per-file
// headers; LICENSE-APACHE in this directory is the upstream notice.
// Modifications are licensed under the same terms.

//! Filename globs from the shared MIME database (`globs` / `globs2` files).
//!
//! Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
//! Copyright (c) Emmanuele Bassi, Apache-2.0. The `glob::Pattern` and
//! `unicase::UniCase` dependencies are replaced by std-only equivalents.
//! `mime::Mime` is replaced by plain `String`s, validated by `is_mime_type`.
//! See the provenance note in `super`.

use std::collections::HashSet;
use std::fmt;
use std::fs::File;
use std::hash::{Hash, Hasher};
use std::io::BufRead;
use std::io::BufReader;
use std::path::Path;

use super::is_mime_type;

/// The stored form of a glob pattern, classified per the pattern's syntax so
/// that the common cases need no general matching.
#[derive(Clone, PartialEq, Eq, Hash)]
enum GlobType {
    /// No wildcards: exact (case-insensitive) file name.
    Literal(String),
    /// `*` only as the leading character: suffix match.
    Simple(String),
    /// Any remaining wildcard syntax, matched by `wildcard_match`.
    Full(String),
}

impl fmt::Debug for GlobType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            GlobType::Literal(name) => write!(f, "Literal '{name}'"),
            GlobType::Simple(pattern) => write!(f, "Simple glob '*{pattern}'"),
            GlobType::Full(pattern) => write!(f, "Full glob '{pattern}'"),
        }
    }
}

impl fmt::Display for GlobType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            GlobType::Literal(str) | GlobType::Simple(str) | GlobType::Full(str) => {
                f.write_str(str)
            }
        }
    }
}

fn glob_type(glob: &str) -> GlobType {
    let mut maybe_simple = false;

    for (idx, ch) in glob.bytes().enumerate() {
        if idx == 0 && ch == b'*' {
            maybe_simple = true;
        } else if matches!(ch, b'\\' | b'[' | b'*' | b'?') {
            return GlobType::Full(glob.to_string());
        }
    }

    if maybe_simple {
        GlobType::Simple(glob[1..].to_string())
    } else {
        GlobType::Literal(glob.to_string())
    }
}

/// Matches one glob pattern against a file name.
///
/// Case-insensitivity: `Literal` and `Simple` fall back to an ASCII-folded
/// comparison unless the `cs` flag is set, matching the database's
/// "match case-insensitively, then sensitively" rule. `Full` patterns are
/// always case-sensitive (upstream xdg-mime parity).
fn matches(glob: &Glob, file_name: &str) -> bool {
    match &glob.glob {
        GlobType::Literal(s) => s.eq_ignore_ascii_case(file_name),
        GlobType::Simple(suffix) => {
            file_name.ends_with(suffix)
                || (!glob.case_sensitive && file_name.to_ascii_lowercase().ends_with(suffix))
        }
        GlobType::Full(pattern) => wildcard_match(pattern, file_name),
    }
}

/// Iterative wildcard match over `Full` patterns: `*`, `?`, `[...]` classes
/// and `\` escapes. Unlike the glob crate, `*` also matches `/` (globs apply
/// to file names, not paths), an unterminated `[` is literal instead of an
/// error, and `{}` alternates are not special (none occur in the database).
fn wildcard_match(pattern: &str, text: &str) -> bool {
    wildcard_match_chars(
        &pattern.chars().collect::<Vec<char>>(),
        &text.chars().collect::<Vec<char>>(),
    )
}

fn wildcard_match_chars(pattern: &[char], text: &[char]) -> bool {
    let mut pi = 0;
    let mut ti = 0;
    // Position after the most recent `*`, and how much of `text` it has
    // consumed; used to backtrack when a later literal mismatches.
    let mut star_pi: Option<usize> = None;
    let mut star_ti = 0;

    while ti < text.len() {
        let matched = match pattern.get(pi) {
            Some('*') => {
                star_pi = Some(pi + 1);
                star_ti = ti;
                pi += 1;
                true
            }
            Some('?') => {
                pi += 1;
                ti += 1;
                true
            }
            Some('\\') if pi + 1 < pattern.len() => {
                if text[ti] == pattern[pi + 1] {
                    pi += 2;
                    ti += 1;
                    true
                } else {
                    false
                }
            }
            Some('[') => {
                match class_end(pattern, pi) {
                    Some(end) => {
                        if class_matches(&pattern[pi + 1..end], text[ti]) {
                            pi = end + 1;
                            ti += 1;
                            true
                        } else {
                            false
                        }
                    }
                    // Unterminated class: literal '['.
                    None => {
                        if text[ti] == '[' {
                            pi += 1;
                            ti += 1;
                            true
                        } else {
                            false
                        }
                    }
                }
            }
            Some(c) if *c == text[ti] => {
                pi += 1;
                ti += 1;
                true
            }
            _ => false,
        };

        if matched {
            continue;
        }

        // Mismatch: let the most recent `*` swallow one more character.
        match star_pi {
            Some(pos) => {
                star_ti += 1;
                ti = star_ti;
                pi = pos;
            }
            None => return false,
        }
    }

    // A leftover pattern can only match the empty tail if it is all stars.
    pattern
        .get(pi..)
        .is_some_and(|rest| rest.iter().all(|c| *c == '*'))
}

/// Index of the `]` closing the class opened at `open`, or `None` if the
/// class is empty or unterminated.
fn class_end(pattern: &[char], open: usize) -> Option<usize> {
    let mut i = open + 1;
    while i < pattern.len() {
        match pattern[i] {
            // A `\` escapes the following character inside the class.
            '\\' => i += 2,
            ']' if i > open + 1 => return Some(i),
            _ => i += 1,
        }
    }
    None
}

/// Whether `c` is in the character class `content` (between `[` and `]`):
/// optional leading `!` negation, `a-z` ranges, `\` escapes.
fn class_matches(content: &[char], c: char) -> bool {
    let (negate, content) = match content.first() {
        Some('!') => (true, &content[1..]),
        _ => (false, content),
    };

    let mut found = false;
    let mut i = 0;
    while i < content.len() {
        match content[i] {
            '\\' if i + 1 < content.len() => {
                if content[i + 1] == c {
                    found = true;
                }
                i += 2;
            }
            '-' if i + 1 < content.len() && i > 0 => {
                // Range: the previous character was already tested as a
                // literal; re-test it as the range start here.
                let start = content[i - 1];
                let end = content[i + 1];
                if start <= c && c <= end {
                    found = true;
                }
                i += 2;
            }
            ch => {
                if ch == c {
                    found = true;
                }
                i += 1;
            }
        }
    }

    found != negate
}

#[derive(Clone)]
pub(super) struct Glob {
    glob: GlobType,
    weight: i32,
    case_sensitive: bool,
    mime_type: String,
}

impl PartialEq for Glob {
    fn eq(&self, other: &Glob) -> bool {
        self.glob == other.glob && self.mime_type == other.mime_type
    }
}

impl Eq for Glob {}

impl Hash for Glob {
    fn hash<H: Hasher>(&self, h: &mut H) {
        self.glob.hash(h);
        self.mime_type.hash(h);
    }
}

impl fmt::Debug for Glob {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Glob: {:?} {:?} (weight: {}, cs: {})",
            self.glob, self.mime_type, self.weight, self.case_sensitive
        )
    }
}

impl Glob {
    fn new(mime_type: &str, glob: &str, weight: i32, cs: bool) -> Glob {
        Glob {
            glob: glob_type(glob),
            weight,
            case_sensitive: cs,
            mime_type: mime_type.to_string(),
        }
    }

    /// One line of the version 1 `globs` file: `mime-type:pattern`.
    fn from_v1_string(s: &str) -> Option<Glob> {
        let mut chunks = s.split(':').fuse();
        let mime_type = chunks.next().filter(|m| is_mime_type(m))?;
        let glob = chunks.next().filter(|s| !s.is_empty())?;

        // The globs file is not extensible, so consume any leftover tokens.
        if chunks.next().is_some() {
            return None;
        }

        Some(Glob::new(mime_type, glob, 50, false))
    }

    /// One line of the version 2 `globs2` file:
    /// `weight:mime-type:pattern[:flags]`.
    fn from_v2_string(s: &str) -> Option<Glob> {
        let mut chunks = s.split(':').fuse();

        let weight = chunks.next()?.parse::<i32>().ok().filter(|n| *n >= 0)?;
        let mime_type = chunks.next().filter(|m| is_mime_type(m))?;
        let glob = chunks.next()?;

        // Only the `cs` flag is understood; extra fields are ignored for
        // extensibility, per the shared-mime-info spec.
        let case_sensitive = chunks
            .next()
            .is_some_and(|flags| flags.split(',').any(|f| f == "cs"));

        Some(Glob::new(mime_type, glob, weight, case_sensitive))
    }

    fn pattern_len(&self) -> usize {
        self.glob.to_string().len()
    }
}

/// Reads a database text file, skipping blank lines and `#` comments.
/// Returns `None` if the file is missing or not valid UTF-8.
pub(super) fn read_lines<P: AsRef<Path>>(file_name: P) -> Option<Vec<String>> {
    let file = File::open(file_name).ok()?;
    let mut lines = Vec::new();
    for line in BufReader::new(file).lines() {
        let line = line.ok()?;
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        lines.push(line);
    }
    Some(lines)
}

fn read_globs_v1_from_file<P: AsRef<Path>>(file_name: P) -> Option<Vec<Glob>> {
    Some(
        read_lines(file_name)?
            .iter()
            .filter_map(|line| Glob::from_v1_string(line))
            .collect(),
    )
}

fn read_globs_v2_from_file<P: AsRef<Path>>(file_name: P) -> Option<Vec<Glob>> {
    Some(
        read_lines(file_name)?
            .iter()
            .filter_map(|line| Glob::from_v2_string(line))
            .collect(),
    )
}

pub(super) fn read_globs_from_dir(dir: &Path) -> Vec<Glob> {
    read_globs_v2_from_file(dir.join("globs2"))
        .or_else(|| read_globs_v1_from_file(dir.join("globs")))
        .unwrap_or_default()
}

pub(super) struct GlobMap {
    globs: HashSet<Glob>,
}

impl GlobMap {
    pub(super) fn new() -> GlobMap {
        GlobMap {
            globs: HashSet::new(),
        }
    }

    pub(super) fn add_globs(&mut self, globs: &[Glob]) {
        self.globs.extend(globs.iter().cloned());
    }

    pub(super) fn lookup_mime_types_for_file_name(&self, file_name: &str) -> Vec<String> {
        let mut matching_globs = Vec::new();

        for glob in &self.globs {
            if matches(glob, file_name) {
                matching_globs.push(glob);
            }
        }

        // "Keep only globs with the biggest weight."
        // -- shared-mime-info, "Recommended checking order"
        let Some(biggest_weight) = matching_globs.iter().map(|glob| glob.weight).max() else {
            return Vec::new();
        };
        matching_globs.retain(|glob| glob.weight == biggest_weight);

        // "If the patterns are different, keep only the globs with the
        // longest pattern." -- must come after the weight filter, which can
        // change which glob is the longest.
        let biggest_pattern_len = matching_globs
            .iter()
            .map(|g| g.pattern_len())
            .max()
            .unwrap_or(0);
        matching_globs.retain(|glob| glob.pattern_len() == biggest_pattern_len);

        matching_globs
            .into_iter()
            .map(|glob| glob.mime_type.clone())
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn glob(line: &str, v2: bool) -> Option<Glob> {
        if v2 {
            Glob::from_v2_string(line)
        } else {
            Glob::from_v1_string(line)
        }
    }

    #[test]
    fn globs_v1_and_v2_parse() {
        let g = glob("text/plain:*.txt", false).unwrap();
        assert_eq!(g.mime_type, "text/plain");
        assert_eq!(g.weight, 50);
        assert!(!g.case_sensitive);

        let g = glob("70:audio/flac:*.flac:cs", true).unwrap();
        assert_eq!(g.mime_type, "audio/flac");
        assert_eq!(g.weight, 70);
        assert!(g.case_sensitive);

        // Extra fields are ignored.
        assert!(glob("70:text/plain:*.txt:cs,future", true).is_some());
    }

    #[test]
    fn globs_reject_malformed_lines() {
        assert!(glob("not-a-mime:*.txt", false).is_none());
        assert!(glob("text/plain:", false).is_none());
        assert!(glob("text/plain:*.txt:extra", false).is_none());
        assert!(glob("-1:text/plain:*.txt", true).is_none());
        assert!(glob("70:not-a-mime:*.txt", true).is_none());
    }

    #[test]
    fn literal_and_simple_matching() {
        // Literal globs are case-insensitive.
        let g = glob("text/plain:Makefile", false).unwrap();
        assert!(matches(&g, "Makefile"));
        assert!(matches(&g, "makefile"));

        // Simple globs match the suffix, case-insensitively unless cs.
        let g = glob("text/plain:*.txt", false).unwrap();
        assert!(matches(&g, "foo.txt"));
        assert!(matches(&g, "foo.TXT"));
        let g = glob("70:text/plain:*.txt:cs", true).unwrap();
        assert!(matches(&g, "foo.txt"));
        assert!(!matches(&g, "foo.TXT"));
    }

    #[test]
    fn full_pattern_matching() {
        let g = |p: &str| Glob::new("text/plain", p, 50, false);
        assert!(matches(&g("file[0-9].dat"), "file7.dat"));
        assert!(!matches(&g("file[0-9].dat"), "fileA.dat"));
        assert!(matches(&g("*.h[ct]"), "main.hc"));
        assert!(!matches(&g("*.h[ct]"), "main.hpp"));
        assert!(matches(&g("[0-9]*"), "42things"));
        assert!(matches(&g("a?c"), "abc"));
        assert!(!matches(&g("a?c"), "ac"));
        assert!(matches(&g("a\\*b"), "a*b"));
        assert!(!matches(&g("a\\*b"), "axb"));
        // Negated class and range at the end (literal '-').
        assert!(matches(&g("[!0-9]file"), "xfile"));
        assert!(!matches(&g("[!0-9]file"), "7file"));
        assert!(matches(&g("[a-]*"), "a-x"));
        // Unterminated class is literal.
        assert!(matches(&g("[weird"), "[weird"));
        // Stars at the end match the empty tail.
        assert!(matches(&g("dir/*"), "dir/"));
    }

    #[test]
    fn wildcard_star_backtracking() {
        assert!(wildcard_match("a*b*c", "axxbyyc"));
        assert!(!wildcard_match("a*b*c", "axxbyy"));
        assert!(wildcard_match("", ""));
        assert!(wildcard_match("***", ""));
        assert!(wildcard_match("***", "x"));
        assert!(wildcard_match("**", "whatever"));
        assert!(wildcard_match("*.ogg", "song.ogg"));
    }

    #[test]
    fn lookup_ranks_by_weight_then_pattern_length() {
        let mut map = GlobMap::new();
        map.add_globs(&[
            Glob::new("application/octet-stream", "*.bin", 10, false),
            Glob::new("application/foo", "*.bin", 50, false),
            Glob::new("application/bar", "special.bin", 50, false),
        ]);

        // Highest weight wins.
        assert_eq!(
            map.lookup_mime_types_for_file_name("foo.bin"),
            ["application/foo".to_string()]
        );
        // On a tie, the longest pattern wins.
        assert_eq!(
            map.lookup_mime_types_for_file_name("special.bin"),
            ["application/bar".to_string()]
        );
        assert!(map.lookup_mime_types_for_file_name("nope.txt").is_empty());
    }
}
