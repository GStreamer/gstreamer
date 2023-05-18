// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

#[cfg(target_os = "macos")]
/// Returns the monotonic system clock in nanoseconds or 0 on error.
pub fn time() -> u64 {
    use std::{
        mem,
        sync::atomic::{self, AtomicU32},
    };

    use crate::ffi::unix::macos::*;

    static TIMEBASE_N: AtomicU32 = AtomicU32::new(0);
    static TIMEBASE_D: AtomicU32 = AtomicU32::new(0);

    let mut timebase_n = TIMEBASE_N.load(atomic::Ordering::Relaxed);
    let mut timebase_d = TIMEBASE_D.load(atomic::Ordering::Relaxed);
    if timebase_n == 0 || timebase_d == 0 {
        // SAFETY: This is safe to call at any time and returns the timebase. Returns 0 on success.
        unsafe {
            let mut timebase = mem::MaybeUninit::uninit();
            if mach_timebase_info(timebase.as_mut_ptr()) != 0 {
                return 0;
            }
            let timebase = timebase.assume_init();
            timebase_n = timebase.numer;
            timebase_d = timebase.denom;

            TIMEBASE_N.store(timebase_n, atomic::Ordering::Relaxed);
            TIMEBASE_D.store(timebase_d, atomic::Ordering::Relaxed);
        }
    }

    // SAFETY: This is safe to call at any time.
    let time = unsafe { mach_absolute_time() };

    (time as u128 * timebase_n as u128 / timebase_d as u128) as u64
}

#[cfg(target_os = "windows")]
/// Returns the monotonic system clock in nanoseconds or 0 on error.
pub fn time() -> u64 {
    use std::{
        mem,
        sync::atomic::{self, AtomicI64},
    };

    use crate::ffi::windows::*;

    static FREQUENCY: AtomicI64 = AtomicI64::new(0);

    let mut freq = FREQUENCY.load(atomic::Ordering::Relaxed);
    if freq == 0 {
        // SAFETY: This is safe to call at any time and will never fail on Windows XP or newer.
        unsafe {
            QueryPerformanceFrequency(&mut freq);
        }
        FREQUENCY.store(freq, atomic::Ordering::Relaxed);
    }

    // SAFETY: This is safe to call at any time and will never fail on Windows XP or newer.
    let time = unsafe {
        let mut time = mem::MaybeUninit::uninit();
        QueryPerformanceCounter(time.as_mut_ptr());
        time.assume_init()
    };

    (time as u128 * 1_000_000_000 / freq as u128) as u64
}

#[cfg(any(
    target_os = "linux",
    target_os = "freebsd",
    target_os = "openbsd",
    target_os = "netbsd",
    target_os = "dragonfly",
    target_os = "solaris",
    target_os = "illumos",
))]
/// Returns the monotonic system clock in nanoseconds or 0 on error.
pub fn time() -> u64 {
    use std::mem;

    use crate::ffi::unix::clock_gettime::*;

    // SAFETY: This is safe to call at any time. 0 will be returned on success, any other value on
    // error.
    unsafe {
        let mut timespec = mem::MaybeUninit::uninit();
        let res = clock_gettime(CLOCK_MONOTONIC, timespec.as_mut_ptr());
        if res == 0 {
            let timespec = timespec.assume_init();

            timespec.tv_sec as u64 * 1_000_000_000 + timespec.tv_nsec as u64
        } else {
            0
        }
    }
}

#[cfg(test)]
mod test {
    #[test]
    fn test_clock() {
        // Check that this doesn't return 0 as that's very likely an indication of things going
        // wrong.
        let now = super::time();
        assert_ne!(now, 0);
    }
}
