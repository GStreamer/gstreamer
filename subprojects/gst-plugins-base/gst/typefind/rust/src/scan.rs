// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Searching a probe window for the pattern that opens a frame.
//!
//! A finder that has to look for a sync word asks the engine for a window of
//! the stream and searches that, rather than peeking a header at every
//! offset. The search runs eight octets at a time: the high bit of each octet
//! says whether it equals a given value, and the masks of the pattern are
//! shifted into place so one word answers for all of its octets.
//!
//! This is the Rust counterpart of `gst_byte_reader_masked_scan_uint32()`: a
//! pattern is a value under a mask, and every octet of it must be present in
//! the data for a match, even the ones the mask clears.

/// A pattern to search for, first octet first.
///
/// `mask` says which octets of `value` matter; an octet the mask clears
/// matches anything. A pattern is one to eight octets, the width of the word
/// the search runs on.
pub(crate) struct Pattern<const N: usize> {
    value: [u8; N],
    mask: [u8; N],
}

impl<const N: usize> Pattern<N> {
    /// A pattern matching `value` where `mask` is set.
    pub(crate) const fn new(value: [u8; N], mask: [u8; N]) -> Self {
        assert!(N >= 1 && N <= 8, "a pattern is one to eight octets");
        Self { value, mask }
    }

    /// The high bit of each octet set where a match starts there in `word`.
    fn hits_in_word(&self, word: u64) -> u64 {
        (0..N).fold(u64::MAX, |hits, i| {
            hits & (octets_equal(word, self.value[i], self.mask[i]) >> (8 * i))
        })
    }

    /// Whether `candidate` is a match.
    fn matches(&self, candidate: &[u8]) -> bool {
        (0..N).all(|i| candidate[i] & self.mask[i] == self.value[i] & self.mask[i])
    }
}

const ONES: u64 = 0x0101_0101_0101_0101;
const LOWS: u64 = 0x7F7F_7F7F_7F7F_7F7F;
const HIGHS: u64 = 0x8080_8080_8080_8080;

/// The high bit of each octet set where that octet matches `value` under
/// `mask`. An octet the mask clears has nothing to differ in, so it matches.
///
/// The zero test is the exact one: `(x - 1) & !x` also marks an octet that is
/// one above a zero octet, because the subtraction borrows into it.
fn octets_equal(word: u64, value: u8, mask: u8) -> u64 {
    let x = (word ^ (u64::from(value) * ONES)) & (u64::from(mask) * ONES);
    // An octet is zero when its low seven bits plus seven do not reach the
    // high bit. Seven never carries out of an octet, so no octet borrows
    // into its neighbour.
    !(((x & LOWS) + LOWS) | x) & HIGHS
}

/// Index of the first match of any of `patterns` in `bytes`.
///
/// The word is loaded once and each pattern answers for it, so a finder that
/// looks for alternatives pays for one walk over the data, not one per
/// alternative.
pub(crate) fn find_any<const N: usize, const M: usize>(
    patterns: &[Pattern<N>; M],
    bytes: &[u8],
) -> Option<usize> {
    // The last octets of a word are seen again in the next one: a match
    // opening there ends past the word.
    let step = 9 - N;
    let mut at = 0;
    while at + 8 <= bytes.len() {
        let word = u64::from_le_bytes(bytes[at..at + 8].try_into().unwrap());
        let hits = patterns
            .iter()
            .fold(0, |hits, pattern| hits | pattern.hits_in_word(word));
        if hits != 0 {
            return Some(at + (hits.trailing_zeros() / 8) as usize);
        }
        at += step;
    }
    bytes[at..]
        .windows(N)
        .position(|candidate| patterns.iter().any(|pattern| pattern.matches(candidate)))
        .map(|hit| at + hit)
}

#[cfg(test)]
mod tests {
    use super::{Pattern, find_any};

    /// Every octet of a match is found, wherever it sits in a word.
    #[test]
    fn a_match_is_found_at_every_offset() {
        const PATTERN: Pattern<3> = Pattern::new([0x00, 0x00, 0x01], [0xFF; 3]);
        for at in 0..24 {
            let mut data = vec![0xFFu8; 32];
            data[at..at + 3].copy_from_slice(&[0x00, 0x00, 0x01]);
            assert_eq!(find_any(&[PATTERN], &data), Some(at), "match at {at}");
        }
    }

    /// The first match is the one answered, not the last.
    #[test]
    fn the_first_match_is_answered() {
        const PATTERN: Pattern<2> = Pattern::new([0xAB, 0xCD], [0xFF; 2]);
        let data = [0x00, 0xAB, 0xCD, 0x00, 0xAB, 0xCD];
        assert_eq!(find_any(&[PATTERN], &data), Some(1));
    }

    /// A cleared octet matches anything, so a masked pattern finds the
    /// candidates its mask leaves open.
    #[test]
    fn a_cleared_octet_matches_anything() {
        const PATTERN: Pattern<2> = Pattern::new([0xFF, 0xF0], [0xFF, 0xF6]);
        // The mask frees the low bit of the second octet.
        let data = [0x00, 0xFF, 0xF1, 0x00];
        assert_eq!(find_any(&[PATTERN], &data), Some(1));
    }

    /// A match that runs past the end of the data is no match.
    #[test]
    fn a_match_past_the_end_is_not_answered() {
        const PATTERN: Pattern<4> =
            Pattern::new([0x00, 0x00, 0x01, 0x00], [0xFF, 0xFF, 0xFF, 0x00]);
        assert_eq!(find_any(&[PATTERN], &[0xFF, 0x00, 0x00, 0x01]), None);
        assert_eq!(
            find_any(&[PATTERN], &[0xFF, 0x00, 0x00, 0x01, 0x00]),
            Some(1)
        );
    }

    /// Nothing to match answers nothing, at any length.
    #[test]
    fn no_match_answers_nothing() {
        const PATTERN: Pattern<3> = Pattern::new([0x00, 0x00, 0x01], [0xFF; 3]);
        for len in 0..24 {
            assert_eq!(
                find_any(&[PATTERN], &vec![0xFFu8; len]),
                None,
                "{len} octets"
            );
        }
    }

    /// The word walk answers exactly what a byte-by-byte scan answers, on
    /// data dense in the octets that make a zero test borrow.
    #[test]
    fn the_word_walk_matches_a_byte_scan() {
        const PATTERN: Pattern<3> = Pattern::new([0x00, 0x00, 0x01], [0xFF; 3]);
        const ALPHABET: [u8; 4] = [0x00, 0x01, 0x02, 0xFF];
        let mut seed = 0x2545_f491_4f6c_dd1du64;
        let mut next = move || {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            seed
        };
        for len in 0..64 {
            for _ in 0..20 {
                let data: Vec<u8> = (0..len)
                    .map(|_| ALPHABET[(next() % ALPHABET.len() as u64) as usize])
                    .collect();
                let exact = data
                    .windows(3)
                    .position(|w| w == [0x00, 0x00, 0x01].as_slice());
                assert_eq!(find_any(&[PATTERN], &data), exact, "{data:02X?}");
            }
        }
    }

    /// The narrowest and the widest pattern both answer.
    #[test]
    fn the_ends_of_the_pattern_width_are_searched() {
        const ONE: Pattern<1> = Pattern::new([0x42], [0xFF]);
        const EIGHT: Pattern<8> = Pattern::new(*b"12345678", [0xFF; 8]);
        let mut data = vec![0x00u8; 32];
        data[20] = 0x42;
        assert_eq!(find_any(&[ONE], &data), Some(20));
        data[8..16].copy_from_slice(b"12345678");
        assert_eq!(find_any(&[EIGHT], &data), Some(8));
    }

    /// The earliest match wins across patterns, whatever their order.
    #[test]
    fn the_earliest_match_wins_across_patterns() {
        const LATE: Pattern<2> = Pattern::new([0xAA, 0xBB], [0xFF; 2]);
        const EARLY: Pattern<2> = Pattern::new([0xCC, 0xDD], [0xFF; 2]);
        let data = [0x00, 0xCC, 0xDD, 0x00, 0xAA, 0xBB];
        assert_eq!(find_any(&[LATE, EARLY], &data), Some(1));
        assert_eq!(find_any(&[EARLY, LATE], &data), Some(1));
    }

    /// A masked octet in the middle of a pattern matches anything.
    #[test]
    fn a_masked_octet_in_the_middle_matches_anything() {
        const PATTERN: Pattern<3> = Pattern::new([0x11, 0x00, 0x33], [0xFF, 0x00, 0xFF]);
        assert_eq!(find_any(&[PATTERN], &[0x00, 0x11, 0x99, 0x33]), Some(1));
    }

    /// A match at the last position of a span longer than a word is found.
    #[test]
    fn a_match_at_the_last_position_is_found() {
        const PATTERN: Pattern<2> = Pattern::new([0xAB, 0xCD], [0xFF; 2]);
        let mut data = vec![0xFFu8; 64];
        data[62..64].copy_from_slice(&[0xAB, 0xCD]);
        assert_eq!(find_any(&[PATTERN], &data), Some(62));
    }
}
