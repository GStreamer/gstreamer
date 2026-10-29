// GStreamer
//
// Copyright (C) 2026 Edward Hervey <edward@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! The start code prefix that opens a syntactic element in an MPEG video
//! elementary stream and in the byte-stream form of the H.26x codecs.
//!
//! Not every codec in the family uses it: H.263 syncs on a 22-bit picture
//! start code that is a bit pattern inside a byte-aligned word, so it scans
//! for octets rather than for this prefix.

use crate::helpers::Scanner;

/// Three octets, `00 00 01`, opening every start code in the formats that
/// frame this way.
const START_CODE_PREFIX: [u8; 3] = [0x00, 0x00, 0x01];

/// How far behind the octet a search leaves the cursor the prefix begins. A
/// caller that wants to know where the start code itself sits has to subtract
/// this.
pub const START_CODE_PREFIX_LEN: i64 = START_CODE_PREFIX.len() as i64;

/// True when `bytes` opens with a start code prefix. The long form `00 00 00 01`
/// carries the prefix one octet further in, so a search that steps a byte at a
/// time reaches it too.
pub fn is_start_code(bytes: &[u8]) -> bool {
    bytes.starts_with(&START_CODE_PREFIX)
}

/// Advance `scan` to the next start code and leave it on the octet that
/// carries the start code's value, which is what says what was started. A
/// prefix with no octet after it names nothing, so a candidate needs four
/// bytes present to count. `limit` is how many bytes past the cursor the
/// search may cross; `None` ends the search, either because nothing
/// qualified within it or because the stream ran out.
pub fn next_start_code(scan: &mut Scanner, limit: usize) -> Option<i64> {
    for _ in 0..=limit {
        if is_start_code(scan.peek(4)?) {
            scan.consume(3);
            return Some(scan.offset());
        }
        scan.consume(1);
    }
    None
}

#[cfg(test)]
mod tests {
    use super::{START_CODE_PREFIX, START_CODE_PREFIX_LEN, is_start_code, next_start_code};
    use crate::helpers::Scanner;
    use crate::helpers::probe_engine::probe;

    #[test]
    fn a_start_code_prefix_is_recognised() {
        assert!(is_start_code(&[0x00, 0x00, 0x01]));
        assert!(is_start_code(&[0x00, 0x00, 0x01, 0xb6]));
    }

    #[test]
    fn each_octet_of_the_prefix_matters() {
        // One octet changed at a time, so a wrong octet is caught where it
        // sits rather than only in combination. The prefix octet itself, left
        // alone, still makes a prefix.
        const PREFIX: [u8; 3] = [0x00, 0x00, 0x01];
        for i in 0..3 {
            for byte in [0x00u8, 0x01, 0x02, 0xff] {
                let mut w = PREFIX;
                w[i] = byte;
                assert_eq!(is_start_code(&w), byte == PREFIX[i], "{w:02X?}");
            }
        }
    }

    #[test]
    fn anything_shorter_is_not_a_start_code() {
        assert!(!is_start_code(&[]));
        assert!(!is_start_code(&[0x00]));
        assert!(!is_start_code(&[0x00, 0x00]));
    }

    #[test]
    fn a_long_start_code_is_reached_by_stepping_one_octet() {
        // The four-octet form is not a prefix match at its own first octet; it
        // is one at the second. This is what makes a one-octet step sufficient.
        let long = [0x00, 0x00, 0x00, 0x01, 0xB6];
        assert!(!is_start_code(&long));
        assert!(is_start_code(&long[1..]));
    }

    /// A limit of nothing still answers once, at the cursor, and leaves it on
    /// the octet that names the start code rather than on its prefix.
    #[test]
    fn a_search_with_no_room_answers_once_at_the_cursor() {
        probe(&[0x00, 0x00, 0x01, 0xB6, 0xFF, 0xFF], |tf| {
            let mut scan = Scanner::new(tf, 0);
            assert_eq!(next_start_code(&mut scan, 0), Some(START_CODE_PREFIX_LEN));
            assert_eq!(scan.offset(), START_CODE_PREFIX_LEN);
        });
    }

    /// The limit counts steps from the cursor, not prefix octets: a prefix
    /// that only comes into view at the last step is still in view, and one
    /// step further is out of it.
    #[test]
    fn a_search_tries_one_position_more_than_its_limit() {
        // `at` is where a search from the cursor at 0 can last land the
        // prefix's first octet; the code octet sits three further on.
        let at = |stream: &[u8], limit: usize| {
            probe(stream, |tf| {
                let mut scan = Scanner::new(tf, 0);
                next_start_code(&mut scan, limit)
            })
        };
        let mut hit = vec![0xFFu8; 12];
        hit[6..9].copy_from_slice(&START_CODE_PREFIX);
        // At the limit, one step short of it, and one step past it.
        assert_eq!(at(&hit, 6), Some(9));
        assert_eq!(at(&hit, 5), None);
        assert_eq!(at(&hit, 7), Some(9));
        // A prefix with no octet after it names nothing and does not answer,
        // however much room the search is given.
        assert_eq!(at(&[0x00, 0x00, 0x01], 8), None);
    }

    /// A search resumes where the last one left the cursor, so two start
    /// codes in one stream answer in turn and neither hands back the other's
    /// position.
    #[test]
    fn a_second_search_from_the_answer_finds_the_next_start_code() {
        probe(
            &[0x00, 0x00, 0x01, 0xB3, 0xFF, 0x00, 0x00, 0x01, 0x00],
            |tf| {
                let mut scan = Scanner::new(tf, 0);
                assert_eq!(next_start_code(&mut scan, 0), Some(3));
                assert_eq!(next_start_code(&mut scan, 4), Some(8));
                assert_eq!(next_start_code(&mut scan, 4), None);
            },
        );
    }
}
