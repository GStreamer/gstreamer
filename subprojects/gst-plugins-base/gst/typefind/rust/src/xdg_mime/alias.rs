// GStreamer
//
// Copyright (c) Emmanuele Bassi
//
// Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
// licensed under the Apache License, Version 2.0. Upstream ships no per-file
// headers; LICENSE-APACHE in this directory is the upstream notice.
// Modifications are licensed under the same terms.

//! MIME type aliases from the shared MIME database (`aliases` file).
//!
//! Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
//! Copyright (c) Emmanuele Bassi, Apache-2.0.
//! See the provenance note in `super`.

use std::path::Path;

use super::glob::read_lines;
use super::is_mime_type;

#[derive(Clone, Debug, PartialEq)]
pub(super) struct Alias {
    alias: String,
    mime_type: String,
}

impl Alias {
    /// One line of the `aliases` file: `alias mime-type`.
    fn from_line(s: &str) -> Option<Alias> {
        let mut chunks = s.split_whitespace().fuse();
        let alias = chunks.next().filter(|m| is_mime_type(m))?;
        let mime_type = chunks.next().filter(|m| is_mime_type(m))?;

        // Any leftover token makes the line invalid.
        if chunks.next().is_some() {
            return None;
        }

        Some(Alias {
            alias: alias.to_string(),
            mime_type: mime_type.to_string(),
        })
    }
}

pub(super) struct AliasesList {
    aliases: Vec<Alias>,
}

impl AliasesList {
    pub(super) fn new() -> AliasesList {
        AliasesList {
            aliases: Vec::new(),
        }
    }

    pub(super) fn add_aliases(&mut self, aliases: Vec<Alias>) {
        self.aliases.extend(aliases);
        self.aliases.sort_by(|a, b| a.alias.cmp(&b.alias));
    }

    /// The canonical MIME type an alias points at, if this is an alias.
    pub(super) fn unalias_mime_type(&self, mime_type: &str) -> Option<String> {
        self.aliases
            .iter()
            .find(|a| a.alias == mime_type)
            .map(|a| a.mime_type.clone())
    }
}

pub(super) fn read_aliases_from_dir(dir: &Path) -> Vec<Alias> {
    read_lines(dir.join("aliases"))
        .map(|lines| {
            lines
                .iter()
                .filter_map(|line| Alias::from_line(line))
                .collect()
        })
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_alias_line() {
        let alias = Alias::from_line("application/x-shellscript text/x-shellscript").unwrap();
        assert_eq!(alias.alias, "application/x-shellscript");
        assert_eq!(alias.mime_type, "text/x-shellscript");
    }

    #[test]
    fn rejects_bad_lines() {
        assert!(Alias::from_line("one/foo two/bar three/baz").is_none());
        assert!(Alias::from_line("no-slash no-slash").is_none());
        assert!(Alias::from_line("one/foo").is_none());
    }

    #[test]
    fn unalias() {
        let mut list = AliasesList::new();
        list.add_aliases(vec![
            Alias::from_line("text/markdown text/x-markdown").unwrap(),
            Alias::from_line("application/x-shellscript text/x-shellscript").unwrap(),
        ]);
        assert_eq!(
            list.unalias_mime_type("text/markdown").as_deref(),
            Some("text/x-markdown")
        );
        assert_eq!(list.unalias_mime_type("text/plain"), None);
    }
}
