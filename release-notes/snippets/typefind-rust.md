## typefind: optional Rust implementation

The typefinders can be built from a Rust implementation instead of the C
sources with `-Dgst-plugins-base:typefind_rust=true`. The C implementation
remains the default; it is expected to switch in 1.32.
