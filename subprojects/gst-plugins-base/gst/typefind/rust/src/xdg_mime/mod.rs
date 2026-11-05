// GStreamer
//
// Copyright (c) Emmanuele Bassi
//
// Derived from the xdg-mime crate v0.4.0 (https://github.com/ebassi/xdg-mime-rs),
// licensed under the Apache License, Version 2.0. Upstream ships no per-file
// headers; LICENSE-APACHE in this directory is the upstream notice.
// Modifications are licensed under the same terms.

//! XDG shared MIME database reader, free of external crates.
//!
//! Derived from the xdg-mime crate v0.4.0
//! (<https://github.com/ebassi/xdg-mime-rs>), Copyright (c) Emmanuele Bassi,
//! Apache-2.0. The logic of the glob and magic matchers and of the guessing
//! pipeline follows upstream; its five dependencies are replaced by std or
//! GLib equivalents so the typefinder crate keeps building offline with no
//! external crates:
//!
//! - `mime` → plain `String`s, validated by [`is_mime_type`] at load time
//!   (RFC 9110 tokens, mirroring the `mime` crate's checks).
//! - `dirs-next` → GLib's XDG base-directory resolution (`g_get_user_data_dir`
//!   and `g_get_system_data_dirs`) in [`data_dirs`].
//! - `unicase` → ASCII case folding. Unicode case folding is lost; database
//!   glob patterns are effectively ASCII.
//! - `glob` → [`glob::wildcard_match`] (`*`, `?`, `[...]`, `\`). No `{}`
//!   alternates (none occur in the database).
//! - `nom` → the cursor parser in [`magic`].
//!
//! Dropped upstream features: icon lookups, and `GuessBuilder` metadata/path
//! inputs (`inode/directory`, `inode/symlink`, `inode/x-empty` handling).
//! Upstream panics on unset `HOME` and on magic rules with a zero-length
//! value; neither panics here.
//!
//! Database files read from each `$XDG_DATA_DIRS/mime/` directory: `globs2`
//! (or legacy `globs`), `magic`, `aliases`, `subclasses`. The compiled
//! `mime.cache` is not used; the text files ship alongside it.

mod alias;
mod glob;
mod magic;
mod parent;

use std::ffi::CStr;
use std::path::{Path, PathBuf};

use crate::bindings;

const APPLICATION_OCTET_STREAM: &str = "application/octet-stream";
const TEXT_PLAIN: &str = "text/plain";

/// Whether `s` is syntactically a MIME type: `type "/" subtype` of RFC 9110
/// tokens. Deliberately permissive: the database contains types that a strict
/// parser rejects, and a false positive here costs nothing.
pub(crate) fn is_mime_type(s: &str) -> bool {
    let Some((top, sub)) = s.split_once('/') else {
        return false;
    };

    fn is_token(s: &str) -> bool {
        !s.is_empty()
            && s.bytes().all(|b| {
                b.is_ascii_alphanumeric()
                    || matches!(
                        b,
                        b'!' | b'#'
                            | b'$'
                            | b'%'
                            | b'&'
                            | b'\''
                            | b'*'
                            | b'+'
                            | b'-'
                            | b'.'
                            | b'^'
                            | b'_'
                            | b'`'
                            | b'|'
                            | b'~'
                    )
            })
    }

    is_token(top) && is_token(sub)
}

/// The MIME `type` portion of `mime` (`"text"` for `"text/plain"`).
fn type_of(mime: &str) -> &str {
    mime.split('/').next().unwrap_or(mime)
}

struct MimeDirectory {
    path: PathBuf,
    mtime: std::time::SystemTime,
}

/// The shared MIME database, loaded from the XDG data directories.
pub(crate) struct SharedMimeInfo {
    aliases: alias::AliasesList,
    parents: parent::ParentsMap,
    globs: glob::GlobMap,
    magic: Vec<magic::MagicEntry>,
    mime_dirs: Vec<MimeDirectory>,
}

impl SharedMimeInfo {
    /// Load the databases from `$XDG_DATA_HOME` and `$XDG_DATA_DIRS`
    /// (each `.../mime/`), per the XDG base directory spec.
    pub(crate) fn new() -> SharedMimeInfo {
        let mut db = SharedMimeInfo::create();
        for dir in data_dirs() {
            db.load_directory(&dir);
        }
        db
    }

    /// Load a single database directory (the directory *containing* `mime/`).
    pub(crate) fn new_for_directory<P: AsRef<Path>>(directory: P) -> SharedMimeInfo {
        let mut db = SharedMimeInfo::create();
        db.load_directory(directory.as_ref());
        db
    }

    fn create() -> SharedMimeInfo {
        SharedMimeInfo {
            aliases: alias::AliasesList::new(),
            parents: parent::ParentsMap::new(),
            globs: glob::GlobMap::new(),
            magic: Vec::new(),
            mime_dirs: Vec::new(),
        }
    }

    fn load_directory(&mut self, directory: &Path) {
        let mime_path = directory.join("mime");

        let aliases = alias::read_aliases_from_dir(&mime_path);
        self.aliases.add_aliases(aliases);

        let subclasses = parent::read_subclasses_from_dir(&mime_path);
        self.parents.add_subclasses(subclasses);

        let globs = glob::read_globs_from_dir(&mime_path);
        self.globs.add_globs(&globs);

        self.magic.extend(magic::read_magic_from_dir(&mime_path));

        let mtime = std::fs::metadata(&mime_path)
            .and_then(|m| m.modified())
            .unwrap_or(std::time::SystemTime::UNIX_EPOCH);
        self.mime_dirs.push(MimeDirectory {
            path: mime_path,
            mtime,
        });
    }

    /// Re-read the databases if any directory changed since loading.
    /// Returns whether anything was reloaded.
    pub(crate) fn reload(&mut self) -> bool {
        let changed = self.mime_dirs.iter().any(|dir| {
            match std::fs::metadata(&dir.path).and_then(|m| m.modified()) {
                Ok(mtime) => dir.mtime < mtime,
                Err(_) => false,
            }
        });
        if !changed {
            return false;
        }

        let directories: Vec<PathBuf> = self
            .mime_dirs
            .iter()
            .filter_map(|dir| dir.path.parent().map(|p| p.to_path_buf()))
            .collect();

        *self = SharedMimeInfo::create();
        for dir in &directories {
            self.load_directory(dir);
        }
        true
    }

    /// All MIME types matching a file name by glob, best first;
    /// `application/octet-stream` if none.
    pub(crate) fn get_mime_types_from_file_name(&self, file_name: &str) -> Vec<String> {
        let mut matches = self.globs.lookup_mime_types_for_file_name(file_name);
        if matches.is_empty() {
            matches.push(APPLICATION_OCTET_STREAM.to_string());
        }
        matches
    }

    /// MIME type matching content by magic rules, with the priority of the
    /// match (>= 80 means certain).
    pub(crate) fn get_mime_type_for_data(&self, data: &[u8]) -> Option<(String, u32)> {
        if data.is_empty() {
            return Some(("application/x-zerosize".to_string(), 100));
        }
        magic::lookup_data(&self.magic, data)
    }

    /// The canonical MIME type an alias points at, if this is an alias.
    pub(crate) fn unalias_mime_type(&self, mime_type: &str) -> Option<String> {
        self.aliases.unalias_mime_type(mime_type)
    }

    /// Whether two MIME types are equal after unaliasing.
    pub(crate) fn mime_type_equal(&self, a: &str, b: &str) -> bool {
        let a = self.unalias_mime_type(a).unwrap_or_else(|| a.to_string());
        let b = self.unalias_mime_type(b).unwrap_or_else(|| b.to_string());
        a == b
    }

    /// Whether `mime_type` is `base`, an alias of it, or a (recursive)
    /// subclass of it.
    pub(crate) fn mime_type_subclass(&self, mime_type: &str, base: &str) -> bool {
        let mime_type = self
            .unalias_mime_type(mime_type)
            .unwrap_or_else(|| mime_type.to_string());
        let base = self
            .unalias_mime_type(base)
            .unwrap_or_else(|| base.to_string());

        if mime_type == base {
            return true;
        }

        // Super-types: `text/*` covers every `text/...` type.
        if let Some(base_top) = base.strip_suffix("/*")
            && type_of(&mime_type) == base_top
        {
            return true;
        }

        // Implicit subclassing from the spec: all text/* under text/plain,
        // everything except inode/* under application/octet-stream.
        if base == TEXT_PLAIN && type_of(&mime_type) == "text" {
            return true;
        }
        if base == APPLICATION_OCTET_STREAM && type_of(&mime_type) != "inode" {
            return true;
        }

        if let Some(parents) = self.parents.lookup(&mime_type)
            && parents.iter().any(|p| self.mime_type_subclass(p, &base))
        {
            return true;
        }

        false
    }

    /// Start guessing a MIME type from a file name and/or content.
    pub(crate) fn guess_mime_type(&self) -> GuessBuilder<'_> {
        GuessBuilder {
            db: self,
            file_name: None,
            data: Vec::new(),
        }
    }
}

impl Default for SharedMimeInfo {
    fn default() -> Self {
        Self::new()
    }
}

/// The data directories of the XDG base directory spec, as GLib resolves
/// them: `$XDG_DATA_HOME` (or `$HOME/.local/share`), then `$XDG_DATA_DIRS`
/// (or the standard `/usr/local/share:/usr/share`).
fn data_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();

    let user = unsafe { bindings::g_get_user_data_dir() };
    if !user.is_null() {
        let user = unsafe { CStr::from_ptr(user) }.to_string_lossy();
        dirs.push(PathBuf::from(user.as_ref()));
    }

    let mut system = unsafe { bindings::g_get_system_data_dirs() };
    while !system.is_null() && unsafe { !(*system).is_null() } {
        let dir = unsafe { CStr::from_ptr(*system) }.to_string_lossy();
        dirs.push(PathBuf::from(dir.as_ref()));
        system = unsafe { system.add(1) };
    }

    dirs
}

fn looks_like_text(data: &[u8]) -> bool {
    // "Checking the first 128 bytes of the file for ASCII
    // control characters is a good way to guess whether a
    // file is binary or text."
    // -- shared-mime-info, "Recommended checking order"
    !data
        .iter()
        .take(128)
        .any(|ch| ch.is_ascii_control() && !ch.is_ascii_whitespace())
}

/// Builder for a MIME type guess.
pub(crate) struct GuessBuilder<'a> {
    db: &'a SharedMimeInfo,
    file_name: Option<String>,
    data: Vec<u8>,
}

impl<'a> GuessBuilder<'a> {
    pub(crate) fn file_name(&mut self, name: &str) -> &mut Self {
        self.file_name = Some(name.to_string());
        self
    }

    pub(crate) fn data(&mut self, data: &[u8]) -> &mut Self {
        // Only keep what any magic rule could possibly read.
        let max = magic::max_extents(&self.db.magic);
        self.data.extend_from_slice(&data[..data.len().min(max)]);
        self
    }

    /// The guessing pipeline of the shared-mime-info spec, "Recommended
    /// checking order" (as sequenced by upstream xdg-mime).
    pub(crate) fn guess(&mut self) -> Guess {
        let name_mime_types = match &self.file_name {
            Some(name) => self.db.get_mime_types_from_file_name(name),
            None => Vec::new(),
        };

        // File name match, and no conflicts.
        if name_mime_types.len() == 1 && name_mime_types[0] != APPLICATION_OCTET_STREAM {
            return Guess {
                mime: name_mime_types[0].clone(),
                uncertain: false,
            };
        }

        let sniffed = match self.data.is_empty() {
            // No content: no magic match. Upstream returns a
            // `application/x-zerosize` hit from a never-read buffer here;
            // the spec assigns that type to empty files, not to absent
            // content.
            true => (APPLICATION_OCTET_STREAM.to_string(), 80),
            false => self
                .db
                .get_mime_type_for_data(&self.data)
                .unwrap_or((APPLICATION_OCTET_STREAM.to_string(), 80)),
        };

        if name_mime_types.is_empty() {
            // No name: whatever the content says.
            return match &self.data[..] {
                [] => Guess {
                    mime: APPLICATION_OCTET_STREAM.to_string(),
                    uncertain: true,
                },
                _ => Guess {
                    mime: sniffed.0.clone(),
                    uncertain: sniffed.0 == APPLICATION_OCTET_STREAM,
                },
            };
        }

        let (mut mime, priority) = sniffed;

        // "If no magic rule matches the data (or if the content is not
        // available), use the default type of application/octet-stream
        // for binary data, or text/plain for textual data."
        // -- shared-mime-info, "Recommended checking order"
        if mime == APPLICATION_OCTET_STREAM && !self.data.is_empty() && looks_like_text(&self.data)
        {
            mime = TEXT_PLAIN.to_string();
        }

        // From GIO: never let content sniffing turn a non-.desktop file
        // into application/x-desktop, which can execute applications.
        // (Upstream parity: this only applies when a file name is known.)
        if self.file_name.is_some() && mime == "application/x-desktop" {
            mime = TEXT_PLAIN.to_string();
        }

        if mime != APPLICATION_OCTET_STREAM {
            if priority >= 80 {
                return Guess {
                    mime,
                    uncertain: false,
                };
            }

            // Content and conflicting names: if the sniffed type is a
            // subclass of one of them, trust the content.
            if name_mime_types
                .iter()
                .any(|m| self.db.mime_type_subclass(&mime, m))
            {
                return Guess {
                    mime,
                    uncertain: false,
                };
            }
        }

        // Conflicts the data cannot resolve: the first name match, uncertain.
        // (With a name present this is the `len() > 1` case; upstream's
        // empty-name branch above is unreachable, kept as fallback.)
        if let Some(mime_type) = name_mime_types.first() {
            return Guess {
                mime: mime_type.clone(),
                uncertain: true,
            };
        }

        Guess {
            mime: APPLICATION_OCTET_STREAM.to_string(),
            uncertain: true,
        }
    }
}

/// The result of a MIME type guess.
pub(crate) struct Guess {
    mime: String,
    uncertain: bool,
}

impl Guess {
    pub(crate) fn mime_type(&self) -> &str {
        &self.mime
    }

    /// Whether the guess had to resolve ambiguity or lack of evidence.
    pub(crate) fn uncertain(&self) -> bool {
        self.uncertain
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mime_type_syntax() {
        assert!(is_mime_type("text/plain"));
        assert!(is_mime_type("application/x-shellscript"));
        assert!(is_mime_type("audio/flac"));
        assert!(!is_mime_type("text"));
        assert!(!is_mime_type("/plain"));
        assert!(!is_mime_type("text/"));
        assert!(!is_mime_type("text /plain"));
        assert!(!is_mime_type("te xt/plain"));
        assert!(!is_mime_type("text/pl(ain"));
    }

    #[test]
    fn subclass_rules() {
        let db = fixture_db();
        // Direct.
        assert!(db.mime_type_subclass("text/rust", "text/plain"));
        // Via the subclasses file (fixture defines it).
        assert!(db.mime_type_subclass("message/partial", "text/plain"));
        // Super-types.
        assert!(db.mime_type_subclass("image/png", "image/*"));
        // Implicit: everything but inode under octet-stream.
        assert!(db.mime_type_subclass("audio/flac", APPLICATION_OCTET_STREAM));
        assert!(!db.mime_type_subclass("inode/directory", APPLICATION_OCTET_STREAM));
        // Not related.
        assert!(!db.mime_type_subclass("image/png", "text/plain"));
    }

    #[test]
    fn aliases() {
        let db = fixture_db();
        assert!(db.mime_type_equal("text/markdown", "text/x-markdown"));
        assert!(!db.mime_type_equal("text/markdown", "text/plain"));
    }

    #[test]
    fn guess_by_name() {
        let db = fixture_db();
        let guess = db.guess_mime_type().file_name("song.flac").guess();
        assert_eq!(guess.mime_type(), "audio/flac");
        assert!(!guess.uncertain());

        // Two globs match `readme.md`; the higher weight (text/markdown,
        // 50) wins over text/plain (30).
        let guess = db.guess_mime_type().file_name("readme.md").guess();
        assert_eq!(guess.mime_type(), "text/markdown");

        // Full-pattern glob with a character class.
        let guess = db.guess_mime_type().file_name("page7.html").guess();
        assert_eq!(guess.mime_type(), "text/html");

        let guess = db.guess_mime_type().file_name("nope.zzz").guess();
        assert_eq!(guess.mime_type(), APPLICATION_OCTET_STREAM);
        assert!(guess.uncertain());
    }

    #[test]
    fn guess_by_data() {
        let db = fixture_db();
        let guess = db.guess_mime_type().data(b"%PDF-1.4 rest").guess();
        assert_eq!(guess.mime_type(), "application/pdf");
        assert!(!guess.uncertain());

        // No rule matches: octet-stream for binary, uncertain.
        let guess = db.guess_mime_type().data(&[0u8; 8]).guess();
        assert_eq!(guess.mime_type(), APPLICATION_OCTET_STREAM);
        assert!(guess.uncertain());
    }

    #[test]
    fn guess_text_fallback() {
        let db = fixture_db();
        // No magic match, textual data, no name → no text/plain promotion
        // without a name conflict (upstream only promotes in the conflict
        // branch; a bare data guess stays octet-stream/uncertain).
        let guess = db.guess_mime_type().data(b"hello").guess();
        assert_eq!(guess.mime_type(), APPLICATION_OCTET_STREAM);
        assert!(guess.uncertain());
    }

    #[test]
    fn data_capped_at_magic_extent() {
        let db = fixture_db();
        let mut gb = db.guess_mime_type();
        gb.data(&vec![b'x'; 4096]);
        assert_eq!(gb.data.len(), magic::max_extents(&db.magic));
    }

    // -- fixture helpers ----------------------------------------------------

    /// A minimal shared MIME database on disk, under a temp dir unique to
    /// the calling test (tests run in parallel). Left behind for
    /// post-mortem inspection.
    fn fixture_db() -> SharedMimeInfo {
        let current = std::thread::current();
        let test_name = current
            .name()
            .and_then(|n| n.rsplit("::").next())
            .unwrap_or("unknown");
        let base = std::env::temp_dir().join(format!(
            "gst-typefind-xdg-mime-fixture-{test_name}-{}",
            std::process::id()
        ));
        let mime = base.join("mime");
        std::fs::create_dir_all(&mime).unwrap();

        std::fs::write(
            mime.join("globs2"),
            concat!(
                "# fixture globs2\n",
                "50:audio/flac:*.flac\n",
                "50:application/zip:*.zip\n",
                "80:text/x-shellscript:*.sh\n",
                "50:text/markdown:*.md\n",
                "30:text/plain:*.md\n",
                "50:text/html:*[0-9].html\n",
            ),
        )
        .unwrap();

        std::fs::write(
            mime.join("aliases"),
            concat!(
                "# fixture aliases\n",
                "text/markdown text/x-markdown\n",
                "application/x-shellscript text/x-shellscript\n",
            ),
        )
        .unwrap();

        std::fs::write(
            mime.join("subclasses"),
            concat!(
                "text/rust text/plain\n",
                "message/partial text/plain\n",
                "audio/flac application/zip\n",
            ),
        )
        .unwrap();

        let mut magic_file = b"MIME-Magic\0\n".to_vec();
        magic_file.extend_from_slice(b"[90:application/pdf]\n");
        magic_file.extend_from_slice(&rule_bytes(0, b"%PDF-"));
        magic_file.extend_from_slice(b"[80:application/x-magic-mp3]\n");
        magic_file.extend_from_slice(&rule_bytes(4, b"ID3"));
        std::fs::write(mime.join("magic"), magic_file).unwrap();

        SharedMimeInfo::new_for_directory(&base)
    }

    fn rule_bytes(offset: u32, value: &[u8]) -> Vec<u8> {
        let mut bytes = format!(">{offset}=").into_bytes();
        bytes.extend_from_slice(&(value.len() as u16).to_be_bytes());
        bytes.extend_from_slice(value);
        bytes.push(b'\n');
        bytes
    }

    #[test]
    #[ignore = "requires the system shared MIME database"]
    fn live_system_database_smoke() {
        let db = SharedMimeInfo::new();
        assert!(!db.magic.is_empty(), "no magic rules parsed");
        assert!(
            !db.globs
                .lookup_mime_types_for_file_name("foo.png")
                .is_empty(),
            "no globs parsed"
        );

        let guess = db.guess_mime_type().file_name("foo.png").guess();
        assert_eq!(guess.mime_type(), "image/png");

        let guess = db.guess_mime_type().data(b"%PDF-1.7\n").guess();
        assert_eq!(guess.mime_type(), "application/pdf");
        assert!(!guess.uncertain());
    }
}
