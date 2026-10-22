# Fuzzing the finders

`cargo-fuzz` over the registered finders, with no GStreamer at run time. The
finders reach the engine through three entry points: peek, get length, and
suggest. `src/lib.rs` defines those over a byte slice and interposes them, so a
target calls the same function pointers the plugin registers and nothing else
differs.

The registry is harvested, not duplicated: the harness runs the crate's own
registration pass with `gst_type_find_register` interposed. A new finder is
covered without any change here.

Needs a nightly toolchain and `cargo-fuzz`. Overflow checks and ASan are on in
the fuzz profile. The meson build turns overflow checks off, so without this an
arithmetic bug wraps silently instead of failing.

## Running it

From this directory, seed the corpora from the fixture set:

    ./seed.sh audio

Then, from the crate directory above:

    cargo +nightly fuzz run audio -- -max_total_time=600 -max_len=16384

`audio` drives the audio finders, where the frame-walking arithmetic lives.
`all` drives every registered finder. Failing inputs land in
`artifacts/<target>/`.

## What a case checks

One byte of finder selector, then the bytes a finder sees. A case fails when a
finder suggests a probability above 100, suggests a NULL media type, suggests a
media type that is not a `type/subtype` pair, or registers a caps string that
would not parse. GStreamer answers the first three with warnings that a fuzzer
walks straight past, so the harness makes them fatal. A caps string that does not
parse is dropped in silence by the real call, which leaves the finder with no
default caps.

## What it cannot find

The oracle only sees what a finder suggests. A finder that fails to detect
anything passes every case. Detection correctness comes from the differential
sweep against the installed build, not from here, and a format needs a fixture
for each mode it has. Joint stereo SBC went undetected through a clean fuzz run
because the fixtures only held mono and plain stereo.

So after a clean run, still add the fixture and run the sweep.

## Coverage

Coverage measures the corpus, not the fuzzing. `cargo fuzz coverage` replays the
inputs already in `fuzz/corpus/<target>` once each and ignores a time limit, so
run the target first to grow that corpus. From the crate directory:

    cargo +nightly fuzz run all -- -max_total_time=300
    cargo +nightly fuzz coverage all
    ~/.rustup/toolchains/nightly-*/lib/rustlib/*/bin/llvm-cov report \
        target/x86_64-unknown-linux-gnu/coverage/x86_64-unknown-linux-gnu/release/all \
        -instr-profile=fuzz/coverage/all/coverage.profdata

After 300 s on `all`: `audio.rs` 98.6% of regions, `helpers.rs` 97.3%,
`subtitles.rs` 97.0%, and the registration table in `typefindfunctions.rs` 58%,
which is where formats with no fixture show up as unreached code.

A few percent right after `./seed.sh` means the run was skipped, not that the
finders are untested. A target that reaches almost nothing reports no findings
just as happily as one that works, so read coverage before believing a quiet
run.

## Adding a finder

1. Write and register it. Nothing here changes.
2. Add a fixture per mode the format has, and add it to the sweep list.
3. `./seed.sh audio`, then run a target.
4. Check coverage covers the new file, as above. If a function is unreached the
   corpus does not exercise it, and the run proved less than it looks.
5. Run the differential sweep against the installed build and compare both the
   caps and the probability.

`cargo +nightly test`, from this directory, runs the harness self-tests. They
pin that the harvest is non-empty, that no selector is dead, that peek answers
with the whole request or nothing, and that a synthetic SBC stream comes back as
`audio/x-sbc`. Run them after touching the harness.
