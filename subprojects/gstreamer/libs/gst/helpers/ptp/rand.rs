// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

#[cfg(unix)]
mod unix {
    use std::io;

    #[cfg(target_os = "linux")]
    /// Try using the getrandom syscall on Linux.
    pub fn getrandom() -> io::Result<[u8; 8]> {
        use std::io::Read;

        use crate::ffi::unix::linux::*;

        // Depends on us knowing the syscall number
        if SYS_getrandom == 0 {
            // FIXME: Use Unsupported once we can depend on 1.53
            return Err(io::Error::from(io::ErrorKind::NotFound));
        }

        struct GetRandom;

        impl Read for GetRandom {
            fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
                // SAFETY: `getrandom` syscall fills up to the requested amount of bytes of
                // the provided memory and returns the number of bytes or a negative value
                // on errors.
                unsafe {
                    let res = syscall(SYS_getrandom, buf.as_mut_ptr(), buf.len(), 0u32);
                    if res < 0 {
                        Err(std::io::Error::last_os_error())
                    } else {
                        Ok(res as usize)
                    }
                }
            }
        }

        let mut r = [0u8; 8];
        GetRandom.read_exact(&mut r)?;

        Ok(r)
    }

    /// Try reading random numbers from /dev/urandom.
    pub fn dev_urandom() -> io::Result<[u8; 8]> {
        use crate::ffi::unix::*;
        use std::{io::Read, os::raw::c_int};

        struct Fd(c_int);

        impl Drop for Fd {
            fn drop(&mut self) {
                // SAFETY: The fd is valid by construction below and closed by this at
                // most once.
                unsafe {
                    // Return value is intentionally ignored as there's nothing that
                    // can be done on errors anyway.
                    let _ = close(self.0);
                }
            }
        }

        impl Read for Fd {
            fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
                // SAFETY: read() requires a valid fd and a mutable buffer with the given size.
                // The fd is valid by construction as is the buffer.
                //
                // read() will return the number of bytes read or a negative value on errors.
                let res = unsafe { read(self.0, buf.as_mut_ptr(), buf.len()) };
                if res < 0 {
                    Err(std::io::Error::last_os_error())
                } else {
                    Ok(res as usize)
                }
            }
        }

        let mut fd = loop {
            // SAFETY: open() requires a NUL-terminated file path and will
            // return an integer in any case. A negative value is an invalid fd
            // and signals an error. On EINTR, opening can be retried.
            let fd = unsafe { open(b"/dev/urandom\0".as_ptr(), O_RDONLY) };
            if fd < 0 {
                let err = std::io::Error::last_os_error();
                if err.kind() == std::io::ErrorKind::Interrupted {
                    continue;
                }

                return Err(err);
            }

            break Fd(fd);
        };

        let mut r = [0u8; 8];
        fd.read_exact(&mut r)?;

        Ok(r)
    }

    #[cfg(test)]
    mod test {
        #[test]
        fn test_dev_urandom() {
            match super::dev_urandom() {
                Ok(n) => {
                    assert_ne!(n, [0u8; 8]);
                }
                Err(err) if err.kind() != std::io::ErrorKind::NotFound => {
                    panic!("{}", err);
                }
                _ => (),
            }
        }

        #[cfg(target_os = "linux")]
        #[test]
        fn test_getrandom() {
            match super::getrandom() {
                Ok(n) => {
                    assert_ne!(n, [0u8; 8]);
                }
                // FIXME: Use Unsupported once we can depend on 1.53
                Err(err) if err.kind() != std::io::ErrorKind::NotFound => {
                    panic!("{}", err);
                }
                _ => (),
            }
        }
    }
}

#[cfg(windows)]
mod windows {
    use std::io;

    /// Call BCryptGenRandom(), which is available since Windows Vista.
    pub fn bcrypt_gen_random() -> io::Result<[u8; 8]> {
        // SAFETY: BCryptGenRandom() fills the provided memory with the requested number of bytes
        // and returns 0 on success. In that case, all memory was written and is initialized now.
        unsafe {
            use std::{mem, ptr};

            use crate::ffi::windows::*;

            let mut r = mem::MaybeUninit::<[u8; 8]>::uninit();
            let res = BCryptGenRandom(
                ptr::null_mut(),
                r.as_mut_ptr() as *mut u8,
                8,
                BCRYPT_USE_SYSTEM_PREFERRED_RNG,
            );
            if res == 0 {
                Ok(r.assume_init())
            } else {
                Err(io::Error::from_raw_os_error(res as i32))
            }
        }
    }

    #[cfg(test)]
    mod test {
        #[test]
        fn test_bcrypt_gen_random() {
            let n = super::bcrypt_gen_random().unwrap();
            assert_ne!(n, [0u8; 8]);
        }
    }
}

/// As fallback use a combination of the process ID and the current system time.
fn fallback_rand() -> [u8; 8] {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::SystemTime::UNIX_EPOCH)
        .unwrap()
        .as_nanos()
        .to_be_bytes();
    let pid = std::process::id().to_be_bytes();
    [
        now[0] ^ now[15] ^ pid[0],
        now[1] ^ now[14] ^ pid[1],
        now[2] ^ now[13] ^ pid[2],
        now[3] ^ now[12] ^ pid[3],
        now[4] ^ now[11] ^ pid[0],
        now[5] ^ now[10] ^ pid[1],
        now[6] ^ now[9] ^ pid[2],
        now[7] ^ now[8] ^ pid[3],
    ]
}

/// Returns a random'ish 64 bit value.
pub fn rand() -> [u8; 8] {
    #[cfg(unix)]
    {
        // Try getrandom syscall or otherwise first on Linux
        #[cfg(target_os = "linux")]
        {
            if let Ok(r) = unix::getrandom() {
                return r;
            }
        }

        if let Ok(r) = unix::dev_urandom() {
            return r;
        }
    }
    #[cfg(windows)]
    {
        if let Ok(r) = windows::bcrypt_gen_random() {
            return r;
        }
    }

    fallback_rand()
}

#[cfg(test)]
mod test {
    // While not a very useful test for randomness, we're mostly interested here
    // in whether the memory is initialized correctly and nothing crashes because
    // of the usage of unsafe code above. If the memory was not initialized fully
    // then this test would fail in e.g. valgrind.
    //
    // Technically, all zeroes could be returned as a valid random number but that's
    // extremely unlikely and more likely a bug in the code above.

    #[test]
    fn test_rand() {
        let n = super::rand();
        assert_ne!(n, [0u8; 8]);
    }

    #[test]
    fn test_fallback_rand() {
        let n = super::fallback_rand();
        assert_ne!(n, [0u8; 8]);
    }
}
