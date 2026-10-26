#!/usr/bin/env bash
# Seed the fuzz corpora from the fixture set.
#
# A case is one byte of finder selector followed by the bytes a finder sees, so
# one seed reaches one finder. Two passes: each fixture under the selector of
# every finder that claims its extension, then one seed per finder so no finder
# starts from nothing.
#
#   ./seed.sh [target] [file ...]
#
# With no files it takes the fixture directories and, when present, the sweep
# lists. Re-running is cheap and keeps the corpora honest as fixtures grow.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
target=${1:-audio}
shift || true

fixtures=${HOME}/work/devel/typefind-fixtures
files=()
if [ "$#" -gt 0 ]; then
    files=("$@")
else
    for dir in "$fixtures"/img "$fixtures"/text "$fixtures"/aac "$fixtures"/dts \
               "$fixtures"/mp3 "$fixtures"/sbc "$fixtures"/wavpack "$fixtures"/mod; do
        [ -d "$dir" ] && files+=("$dir"/*)
    done
    for list in /tmp/imgtext.list /tmp/audio.list /tmp/corpus.list; do
        [ -f "$list" ] || continue
        while IFS= read -r line; do
            [ -n "$line" ] && files+=("$line")
        done < <(grep -v '^#' "$list" | grep -v '^[[:space:]]*$')
    done
fi

# The selector is a raw byte and byte zero is a NUL, which no shell argument
# can carry, so it goes out through a binary-safe writer.
sel() { python3 -c 'import sys; sys.stdout.buffer.write(bytes([int(sys.argv[1])]))' "$1"; }

# The selector space is the registry, so ask it rather than hardcode a count.
table=$(cd "$here" && cargo +nightly test -q -- --ignored \
            --nocapture 2>/dev/null | grep -E '^[0-9]+[[:space:]]')
count=$(grep -c . <<<"$table")
[ "$count" -gt 0 ] || { echo "could not read the registry" >&2; exit 1; }

# A fixture has to reach the finder that claims its extension. A rotating
# selector feeds a .wv file to whatever finder lands on it, which only ever
# exercises the rejection path, and a finder whose format needs a real header is
# never reached that way.
declare -A by_ext=()
while IFS=$'\t' read -r idx name exts; do
    [ -n "${exts:-}" ] || continue
    IFS=',' read -ra ext_list <<<"$exts"
    for e in "${ext_list[@]}"; do by_ext["$e"]+="$idx "; done
done <<<"$table"

# Longest case the fuzzer will consider, and the seed ceiling that goes with it.
max_len=16384

out="$here/corpus/$target"
mkdir -p "$out"
rm -rf "${out:?}/"*

n=0
for f in "${files[@]}"; do
    [ -f "$f" ] || continue
    ext=${f##*.}
    ext=${ext,,}
    # With no finder registered for the extension, fall back to the rotation so
    # the corpus still spreads across the selector space.
    selectors=${by_ext[$ext]:-$((n % count))}
    for s in $selectors; do
        {
            sel "$s"
            head -c "$max_len" "$f"
        } > "$out/seed-$(printf %04d "$n")-$s-$(basename "$f")"
    done
    n=$((n + 1))
done

# One seed per finder, from the smallest fixture, so every selector value has a
# starting point even for finders no fixture matches.
smallest=
size=$((max_len + 1))
for f in "${files[@]}"; do
    [ -f "$f" ] || continue
    s=$(stat -c '%s' "$f")
    if [ "$s" -lt "$size" ]; then size=$s; smallest=$f; fi
done
if [ -n "$smallest" ]; then
    for i in $(seq 0 $((count - 1))); do
        {
            sel "$i"
            head -c "$max_len" "$smallest"
        } > "$out/each-$(printf %04d "$i")"
    done
fi

echo "seeded $out: $n fixture seeds + $count per-finder seeds"
