// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use crate::error::Error;

#[cfg(ptp_helper_permissions = "setcap")]
mod setcap {
    use super::*;

    use crate::{error::Context, ffi::unix::setcaps::*};
    use std::io;

    struct Cap(cap_t);

    impl Drop for Cap {
        fn drop(&mut self) {
            // SAFETY: The capabilities are valid by construction and are only dropped
            // once here.
            unsafe {
                let _ = cap_free(self.0);
            }
        }
    }

    impl Cap {
        /// Get the current process' capabilities.
        fn get_proc() -> io::Result<Self> {
            //  SAFETY: Get the current capabilities of the process. This returns NULL on error or
            //  otherwise newly allocated capabilities that have to be freed again in the end. For
            //  that purpose we wrap them in the Cap struct.
            unsafe {
                let c = cap_get_proc();
                if c.is_null() {
                    return Err(io::Error::last_os_error());
                }

                Ok(Cap(c))
            }
        }

        /// Clear all capabilities.
        fn clear(&mut self) -> io::Result<()> {
            // SAFETY: Clearing all current capabilities. This requires a valid capabilities
            // pointer, which we have at this point by construction.
            unsafe {
                if cap_clear(self.0) != 0 {
                    return Err(io::Error::last_os_error());
                }
            }

            Ok(())
        }

        /// Set current process' capabilities.
        fn set_proc(&self) -> io::Result<()> {
            // SAFETY: Setting the current process's capabilities, which is only affecting the
            // current thread unfortunately. At this point, no other threads were started yet so
            // this is not a problem. Also the capabilities pointer is still valid by construction.
            unsafe {
                if cap_set_proc(self.0) != 0 {
                    return Err(io::Error::last_os_error());
                }
            }
            Ok(())
        }
    }

    /// Drop all current capabilities of the process.
    pub fn drop() -> Result<(), Error> {
        let mut c = Cap::get_proc().context("Failed to get current process capabilities")?;
        c.clear().context("Failed to clear capabilities")?;
        c.set_proc()
            .context("Failed to set current process capabilities")?;

        Ok(())
    }

    #[cfg(test)]
    mod test {
        #[test]
        fn test_get_set_same_and_clear_cap() {
            let mut c = super::Cap::get_proc().unwrap();
            // Setting the same capabilities should always succeed
            c.set_proc().unwrap();
            c.clear().unwrap();
        }
    }
}

#[cfg(ptp_helper_permissions = "setuid-root")]
mod setuid_root {
    use super::*;

    use crate::{bail, error::Context, ffi::unix::setuid_root::*};
    use std::{ffi::CString, io};

    /// Retrieve UID and GID for the given username.
    fn get_uid_gid_for_user(name: &str) -> io::Result<(uid_t, gid_t)> {
        let name_cstr = CString::new(name).unwrap();

        loop {
            // SAFETY: getpwnam() requires a NUL-terminated user name string and
            // returns either the user information in static storage, or NULL on error.
            // In case of EINTR, getting the user information can be retried.
            //
            // The user information is stored in static storage so might change if something
            // else calls related functions. As this is the only thread up to this point and we
            // just extract two integers from it there is no such possibility.
            unsafe {
                let pw = getpwnam(name_cstr.as_ptr());
                if pw.is_null() {
                    let err = io::Error::last_os_error();
                    if err.kind() == io::ErrorKind::Interrupted {
                        continue;
                    }
                    if err.raw_os_error() == Some(0) {
                        return Err(io::Error::from(io::ErrorKind::NotFound));
                    }

                    return Err(err);
                }
                return Ok(((*pw).pw_uid, (*pw).pw_gid));
            }
        }
    }

    /// Retrieve GID for the given group name.
    fn get_gid_for_group(name: &str) -> io::Result<gid_t> {
        let name_cstr = CString::new(name).unwrap();
        loop {
            // SAFETY: getgrnam() requires a NUL-terminated group name string and
            // returns either the group information in static storage, or NULL on error.
            // In case of EINTR, getting the group information can be retried.
            //
            // The user information is stored in static storage so might change if something
            // else calls related functions. As this is the only thread up to this point and we
            // just extract two integers from it there is no such possibility.
            unsafe {
                let grp = getgrnam(name_cstr.as_ptr());
                if grp.is_null() {
                    let err = io::Error::last_os_error();
                    if err.kind() == io::ErrorKind::Interrupted {
                        continue;
                    }
                    if err.raw_os_error() == Some(0) {
                        return Err(io::Error::from(io::ErrorKind::NotFound));
                    }

                    return Err(err);
                }

                return Ok((*grp).gr_gid);
            }
        }
    }

    #[cfg(test)]
    mod test {
        #[test]
        fn test_get_uid_gid_for_user() {
            match super::get_uid_gid_for_user("root") {
                Ok(_) => (),
                Err(err) if err.kind() != std::io::ErrorKind::NotFound => {
                    panic!("{}", err);
                }
                _ => (),
            }
        }

        #[test]
        fn test_get_gid_for_group() {
            match super::get_gid_for_group("root") {
                Ok(_) => (),
                Err(err) if err.kind() != std::io::ErrorKind::NotFound => {
                    panic!("{}", err);
                }
                _ => (),
            }
        }
    }

    /// Drop the process's UID/GID from root to the configured user/group or the user "nobody".
    pub fn drop() -> Result<(), Error> {
        let username = gst_ptp_helper_conf::PTP_HELPER_SETUID_USER.unwrap_or("nobody");

        let (uid, gid) = get_uid_gid_for_user(username)
            .with_context(|| format!("Failed to get user information for {}", username))?;
        let gid = if let Some(group) = gst_ptp_helper_conf::PTP_HELPER_SETUID_GROUP {
            get_gid_for_group(group)
                .with_context(|| format!("Failed to get group information for {}", group))?
        } else {
            gid
        };

        // SAFETY: This function can be called at any time and never fails.
        let old_gid = unsafe { getgid() };

        // SAFETY: Changes the effective group id of the process and return zero on success.
        unsafe {
            if setgid(gid) != 0 {
                bail!(
                    source: io::Error::last_os_error(),
                    "Failed to set group id {} for process",
                    gid,
                );
            }
        }

        // SAFETY: Changes the effective user id of the process and return zero on success.
        // On errors, try resetting to the old gid just to be sure.
        unsafe {
            if setuid(uid) != 0 {
                let err = io::Error::last_os_error();
                let _ = setgid(old_gid);
                bail!(source: err, "Failed to set user id {} for process", uid);
            }
        }

        Ok(())
    }
}

/// Drop all additional permissions / capabilities the current process might have as they're not
/// needed anymore.
///
/// This does nothing if no such mechanism is implemented / selected for the target platform.
pub fn drop() -> Result<(), Error> {
    #[cfg(ptp_helper_permissions = "setcap")]
    {
        setcap::drop()?;
    }
    #[cfg(ptp_helper_permissions = "setuid-root")]
    {
        setuid_root::drop()?;
    }

    Ok(())
}
