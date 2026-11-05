// GStreamer
//
// Copyright (c) Emmanuele Bassi
//
// Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
// licensed under the Apache License, Version 2.0. Upstream ships no per-file
// headers; LICENSE-APACHE in this directory is the upstream notice.
// Modifications are licensed under the same terms.

//! MIME type subclass relations from the shared MIME database (`subclasses`
//! file).
//!
//! Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
//! Copyright (c) Emmanuele Bassi, Apache-2.0.
//! See the provenance note in `super`.

use std::collections::HashMap;
use std::path::Path;

use super::glob::read_lines;
use super::is_mime_type;

#[derive(Clone, Debug, PartialEq)]
pub(super) struct Subclass {
    mime_type: String,
    parent_type: String,
}

impl Subclass {
    /// One line of the `subclasses` file: `mime-type parent-type`.
    fn from_line(s: &str) -> Option<Subclass> {
        let mut chunks = s.split_whitespace().fuse();
        let mime_type = chunks.next().filter(|m| is_mime_type(m))?;
        let parent_type = chunks.next().filter(|m| is_mime_type(m))?;

        // Any leftover token makes the line invalid.
        if chunks.next().is_some() {
            return None;
        }

        Some(Subclass {
            mime_type: mime_type.to_string(),
            parent_type: parent_type.to_string(),
        })
    }
}

pub(super) struct ParentsMap {
    parents: HashMap<String, Vec<String>>,
}

impl ParentsMap {
    pub(super) fn new() -> ParentsMap {
        ParentsMap {
            parents: HashMap::new(),
        }
    }

    fn add_subclass(&mut self, subclass: Subclass) {
        let parents = self.parents.entry(subclass.mime_type).or_default();
        if !parents.contains(&subclass.parent_type) {
            parents.push(subclass.parent_type);
        }
    }

    pub(super) fn add_subclasses(&mut self, subclasses: Vec<Subclass>) {
        for subclass in subclasses {
            self.add_subclass(subclass);
        }
    }

    pub(super) fn lookup(&self, mime_type: &str) -> Option<&[String]> {
        self.parents.get(mime_type).map(Vec::as_slice)
    }
}

pub(super) fn read_subclasses_from_dir(dir: &Path) -> Vec<Subclass> {
    read_lines(dir.join("subclasses"))
        .map(|lines| {
            lines
                .iter()
                .filter_map(|line| Subclass::from_line(line))
                .collect()
        })
        .unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_subclass_line() {
        let subclass = Subclass::from_line("message/partial text/plain").unwrap();
        assert_eq!(subclass.mime_type, "message/partial");
        assert_eq!(subclass.parent_type, "text/plain");
    }

    #[test]
    fn rejects_bad_lines() {
        assert!(Subclass::from_line("one/foo two/bar three/baz").is_none());
        assert!(Subclass::from_line("no-slash/ no-slash/").is_none());
    }

    #[test]
    fn parent_map() {
        let mut map = ParentsMap::new();
        map.add_subclasses(vec![
            Subclass::from_line("message/partial text/plain").unwrap(),
            Subclass::from_line("text/rfc822-headers text/plain").unwrap(),
            // Duplicates are not added twice.
            Subclass::from_line("message/partial text/plain").unwrap(),
        ]);
        let expected: &[String] = &["text/plain".to_string()];
        assert_eq!(map.lookup("message/partial"), Some(expected));
        assert_eq!(map.lookup("text/plain"), None);
    }
}
