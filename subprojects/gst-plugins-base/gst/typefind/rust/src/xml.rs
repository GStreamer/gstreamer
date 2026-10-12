// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

const BOM: &[u8] = b"\xEF\xBB\xBF";
const DOCTYPE: &[u8] = b"<!DOCTYPE";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct XmlProbe<'a> {
    /// An `<?xml ...?>` declaration is present. Not set while the input
    /// ends mid `<?xml`, where a `<?xml-...` PI is still possible.
    pub has_prolog: bool,
    /// The DOCTYPE name as written, if determined. The `<!DOCTYPE` keyword is
    /// matched ignoring ASCII case; the name is not folded.
    pub doctype: Option<&'a str>,
    /// The root element name, if determined.
    pub first_element: Option<&'a str>,
}

// Probe the first bytes of a stream for an XML 1.0 document.
//
// The input is a prefix that may continue past the end. `xml_probe` returns
// `None` until XML is established: the input starts with `<?xml`, or a
// declaration, a DOCTYPE name, or the root element name is determined.
pub fn xml_probe(data: &[u8]) -> Option<XmlProbe<'_>> {
    let bytes = data.strip_prefix(BOM).unwrap_or(data);

    let (has_prolog, doctype, rest) = skip_prolog_components(bytes)?;

    // `rest` starts at the root tag, or is empty when the input ended
    // before it.
    let first_element = match rest.strip_prefix(b"<") {
        Some(name) if !name.is_empty() && is_name_start(name[0]) => {
            match name.iter().position(|b| !is_name_byte(*b)) {
                // A name is confirmed by a legal follower (whitespace, `/`
                // or `>`), not by EOF.
                Some(e) if matches!(name[e], b' ' | b'\t' | b'\r' | b'\n' | b'/' | b'>') => {
                    std::str::from_utf8(&name[..e]).ok()
                }
                _ => None,
            }
        }
        _ => None,
    };

    (has_prolog || doctype.is_some() || first_element.is_some() || bytes.starts_with(b"<?xml"))
        .then_some(XmlProbe {
            has_prolog,
            doctype,
            first_element,
        })
}

/// Skip prolog components; return `(declaration, doctype name, bytes
/// starting at the root tag)`. An empty remainder means the input ended
/// before the root tag.
fn skip_prolog_components(bytes: &[u8]) -> Option<(bool, Option<&str>, &[u8])> {
    // `<?xml` followed by whitespace or `?>` at offset 0 is the
    // declaration; anything else (e.g. `<?xml-stylesheet`) is a PI, handled
    // in the loop.
    let (prolog, mut rest) = if let Some(tail) = bytes.strip_prefix(b"<?xml")
        && ((!tail.is_empty() && is_ws(tail[0])) || tail.starts_with(b"?>"))
        && let Some(terminated) = after(b"?>", tail)
    {
        (true, terminated)
    } else {
        (false, bytes)
    };

    let mut doctype: Option<&str> = None;
    loop {
        // `Misc*`: whitespace, comments and PIs may appear anywhere in the
        // prolog, including before the first component.
        rest = &rest[rest.iter().position(|b| !is_ws(*b)).unwrap_or(rest.len())..];
        if rest.is_empty() {
            return Some((prolog, doctype, rest));
        }
        if let Some(inner) = rest.strip_prefix(b"<!--") {
            // `-->` cannot occur inside a comment.
            match after(b"-->", inner) {
                Some(r) => rest = r,
                None => return Some((prolog, doctype, b"")),
            }
        } else if starts_with_ignore_ascii_case(rest, DOCTYPE) {
            // `<!doctype html>` is common, though XML requires the uppercase
            // form.
            let inner = &rest[DOCTYPE.len()..];
            // `<!DOCTYPE` S Name: the name starts at the first non-space.
            match inner.iter().position(|b| !is_ws(*b)) {
                None => {} // name not visible yet
                Some(i) if i == 0 || !is_name_start(inner[i]) => {
                    return None; // e.g. `<!DOCTYPEroot>`, `<!DOCTYPE >`
                }
                Some(i) => {
                    // A name running to EOF is not confirmed.
                    doctype = inner[i..]
                        .iter()
                        .position(|b| !is_name_byte(*b))
                        .and_then(|end| std::str::from_utf8(&inner[i..i + end]).ok());
                }
            }
            // Known limitation: the first `>` wins, so a `>` in `SYSTEM "a>b"`
            // or in an internal subset ends the declaration early.
            match after(b">", inner) {
                Some(r) => rest = r,
                None => return Some((prolog, doctype, b"")),
            }
        } else if rest.starts_with(b"<!") && starts_with_ignore_ascii_case(DOCTYPE, rest) {
            // Partial `<!DOCTYPE`; the only other valid `<!` here is a comment.
            return Some((prolog, doctype, b""));
        } else if let Some(inner) = rest.strip_prefix(b"<?") {
            // `?>` cannot occur inside a PI.
            match after(b"?>", inner) {
                Some(r) => {
                    // The target is everything between `<?` and `?>`.
                    let target = &inner[..inner.len() - r.len() - b"?>".len()];
                    // `xml` in any case is not a PI target, and a
                    // declaration is only legal at offset 0.
                    if target.eq_ignore_ascii_case(b"xml")
                        || target
                            .get(3)
                            .is_some_and(|b| target.starts_with(b"xml") && is_ws(*b))
                    {
                        return None;
                    }
                    rest = r;
                }
                None => return Some((prolog, doctype, b"")),
            }
        } else if rest.starts_with(b"<") {
            break; // root tag
        } else {
            return None; // neither a prolog component nor a root tag
        }
    }
    Some((prolog, doctype, rest))
}

fn is_ws(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\r' | b'\n')
}

// XML name characters, approximated at byte level: any byte >= 0x80 counts
// as part of a UTF-8 name character.
fn is_name_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_' || b == b':' || b >= 0x80
}

fn is_name_byte(b: u8) -> bool {
    is_name_start(b) || b.is_ascii_digit() || b == b'-' || b == b'.'
}

/// The slice after the first `term`; `None` if absent.
fn after<'a>(term: &[u8], bytes: &'a [u8]) -> Option<&'a [u8]> {
    bytes
        .windows(term.len())
        .position(|w| w == term)
        .map(|i| &bytes[i + term.len()..])
}

fn starts_with_ignore_ascii_case(haystack: &[u8], prefix: &[u8]) -> bool {
    haystack.len() >= prefix.len() && haystack[..prefix.len()].eq_ignore_ascii_case(prefix)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn xml<'a>(
        prolog: bool,
        doctype: Option<&'a str>,
        root: Option<&'a str>,
    ) -> Option<XmlProbe<'a>> {
        Some(XmlProbe {
            has_prolog: prolog,
            doctype,
            first_element: root,
        })
    }

    #[test]
    fn root_element() {
        // The whole element name is reported, as written.
        assert_eq!(xml_probe(b"<root>"), xml(false, None, Some("root")));
        assert_eq!(
            xml_probe(b"<root attr=\"v\">child"),
            xml(false, None, Some("root"))
        );
        assert_eq!(
            xml_probe(b"<ns:el-1_2.3>"),
            xml(false, None, Some("ns:el-1_2.3"))
        );
        assert_eq!(
            xml_probe(b"<\xC3\xA9l\xC3\xA9ment>"),
            xml(false, None, Some("\u{e9}l\u{e9}ment"))
        );
        assert_eq!(xml_probe(b"<tt>"), xml(false, None, Some("tt")));
        assert_eq!(xml_probe(b"<ttt>"), xml(false, None, Some("ttt")));
        assert_eq!(xml_probe(b"<HTML>"), xml(false, None, Some("HTML")));
        // Only the name is looked at; bytes after it are not inspected.
        assert_eq!(xml_probe(b"<MPD \x00\x01>"), xml(false, None, Some("MPD")));
        // `=` or an illegal byte directly after the name is not a start tag.
        assert_eq!(xml_probe(b"<root=a>"), None);
        assert_eq!(xml_probe(b"<root!>"), None);
        assert_eq!(xml_probe(b"<\xFF>"), None);
    }

    #[test]
    fn declaration() {
        assert_eq!(
            xml_probe(b"<?xml version=\"1.0\"?>\n<root>"),
            xml(true, None, Some("root"))
        );
        assert_eq!(xml_probe(b"<?xml?><r>"), xml(true, None, Some("r"))); // version not checked
        // `xml` in any case is reserved: a declaration away from offset 0 is
        // not XML.
        assert_eq!(xml_probe(b"<?xmlfoo?><r>"), xml(false, None, Some("r")));
        assert_eq!(
            xml_probe(b"<?xml-stylesheet href=\"x\"?><r>"),
            xml(false, None, Some("r"))
        );
        assert_eq!(xml_probe(b"<?XML?><r>"), None);
        assert_eq!(xml_probe(b"<!--c--><?xml version=\"1.0\"?><r>"), None);
        // The declaration must come first.
        assert_eq!(xml_probe(b" <?xml v?><r/>"), None);
    }

    #[test]
    fn doctype() {
        assert_eq!(
            xml_probe(b"<!DOCTYPE root>\n<root>"),
            xml(false, Some("root"), Some("root"))
        );
        assert_eq!(
            xml_probe(b"<!DOCTYPE  html>"),
            xml(false, Some("html"), None)
        );
        assert_eq!(
            xml_probe(b"<!DOCTYPE html PUBLIC \"-//W3C//DTD\" \"x.dtd\">"),
            xml(false, Some("html"), None)
        );
        // Keyword in any case, name as written.
        assert_eq!(
            xml_probe(b"<!DOCTYPE HTML><HTML>"),
            xml(false, Some("HTML"), Some("HTML"))
        );
        assert_eq!(
            xml_probe(b"<!doctype html>"),
            xml(false, Some("html"), None)
        );
        assert_eq!(
            xml_probe(b"<!DocType html><html>"),
            xml(false, Some("html"), Some("html"))
        );
        // Whitespace and a name are mandatory; a name running to EOF is not
        // confirmed.
        assert_eq!(xml_probe(b"<!DOCTYPEroot>"), None);
        assert_eq!(xml_probe(b"<!DOCTYPE >"), None);
        assert_eq!(xml_probe(b"<!DOCTYPE root"), None);
        assert_eq!(xml_probe(b"<!bogus>"), None);
        // Known limitation: the first `>` wins (see `skip_prolog_components`).
        assert_eq!(xml_probe(b"<!DOCTYPE r SYSTEM \"a>b\"><r>"), None);
        assert_eq!(xml_probe(b"<!DOCTYPE r [ <!ENTITY x \"y\"> ]><r>"), None);
    }

    #[test]
    fn bom() {
        assert_eq!(
            xml_probe(b"\xEF\xBB\xBF<?xml version=\"1.0\"?><x/>"),
            xml(true, None, Some("x"))
        );
        assert_eq!(xml_probe(b"\xEF\xBB\xBF<y>"), xml(false, None, Some("y")));
        // Only one BOM is stripped.
        assert_eq!(xml_probe(b"\xEF\xBB\xBF\xEF\xBB\xBF<r>"), None);
        assert_eq!(xml_probe(b"\xEF<r>"), None);
    }

    #[test]
    fn none_until_xml_is_established() {
        // Too short to determine a declaration, a DOCTYPE name or a root
        // element name.
        assert_eq!(xml_probe(b""), None);
        assert_eq!(xml_probe(b"<"), None);
        assert_eq!(xml_probe(b"<root"), None);
        assert_eq!(xml_probe(b"<?xm"), None);
        assert_eq!(xml_probe(b"<!-- c"), None);
        assert_eq!(xml_probe(b"<!DOCTYPE"), None);
        assert_eq!(xml_probe(b"\xEF"), None);
        assert_eq!(xml_probe(b"\xEF\xBB\xBF"), None);
        // A declaration, a DOCTYPE name or a root element name is enough.
        assert_eq!(xml_probe(b"<?xml"), xml(false, None, None));
        assert_eq!(xml_probe(b"<?xml version=\"1.0\""), xml(false, None, None));
        assert_eq!(
            xml_probe(b"<?xml version=\"1.0\"?> "),
            xml(true, None, None)
        );
        assert_eq!(
            xml_probe(b"<!DOCTYPE root>"),
            xml(false, Some("root"), None)
        );
        // Not XML at all.
        assert_eq!(xml_probe(b"hello"), None);
        assert_eq!(xml_probe(b"</root>"), None);
        assert_eq!(xml_probe(b"< root>"), None);
        assert_eq!(xml_probe(b"<1root>"), None);
        assert_eq!(xml_probe(b"%PDF-1.4"), None);
    }

    #[test]
    fn result_borrows_input() {
        fn in_input<'a>(hay: &'a [u8], needle: &'a str) {
            let n = needle.as_bytes();
            assert!(hay.windows(n.len()).any(|w| w == n));
        }
        let data = b"<!DOCTYPE root>\n<root>".as_slice();
        let probe = xml_probe(data).unwrap();
        in_input(data, probe.doctype.unwrap());
        in_input(data, probe.first_element.unwrap());
    }

    #[test]
    fn misc_around_components() {
        // `prolog ::= XMLDecl? Misc*`, `Misc ::= Comment | PI | S`.
        assert_eq!(
            xml_probe(b" \n\t<!-- c --> <root>"),
            xml(false, None, Some("root"))
        );
        assert_eq!(xml_probe(b"<!----><r>"), xml(false, None, Some("r")));
        // `<` and `--` inside a comment do not end it.
        assert_eq!(
            xml_probe(b"<!-- a < b -- c --><r>"),
            xml(false, None, Some("r"))
        );
        assert_eq!(
            xml_probe(b"<?xml v?>\n<!-- c --> <?meta?> <!DOCTYPE r> <r>"),
            xml(true, Some("r"), Some("r"))
        );
    }
}
