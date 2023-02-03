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

/// Drop all additional permissions / capabilities the current process might have as they're not
/// needed anymore.
///
/// This does nothing if no such mechanism is implemented / selected for the target platform.
pub fn drop() -> Result<(), Error> {
    #[cfg(ptp_helper_permissions = "setcap")]
    {
        // Drop all current capabilities of the process.

        use std::io;

        use crate::{bail, ffi::unix::setcaps::*};

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

        // SAFETY: There are 3 steps here
        //  1. Get the current capabilities of the process. This
        //     returns NULL on error or otherwise newly allocated capabilities that have to be
        //     freed again in the end. For that purpose we wrap them in the Cap struct.
        //
        //  2. Clearing all current capabilities. This requires a valid capabilities pointer,
        //     which we have at this point by construction.
        //
        //  3. Setting the current process's capabilities, which is only affecting the current
        //     thread unfortunately. At this point, no other threads were started yet so this is
        //     not a problem. Also the capabilities pointer is still valid by construction.
        //
        //  On every return path, the capabilities are going to be freed.
        unsafe {
            let c = cap_get_proc();
            if c.is_null() {
                bail!(
                    source: io::Error::last_os_error(),
                    "Failed to get current process capabilities"
                );
            }

            let c = Cap(c);
            if cap_clear(c.0) != 0 {
                bail!(
                    source: io::Error::last_os_error(),
                    "Failed to clear capabilities"
                );
            }
            if cap_set_proc(c.0) != 0 {
                bail!(
                    source: io::Error::last_os_error(),
                    "Failed to set current process capabilities"
                );
            }
        }
    }
    #[cfg(ptp_helper_permissions = "setuid-root")]
    {
        // Drop the process's UID/GID from root to the configured user/group or the user "nobody".

        use std::{ffi::CString, io};

        use crate::{bail, error::Context, ffi::unix::setuid_root::*};

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
                        return Err(err);
                    }
                    return Ok(((*pw).pw_uid, (*pw).pw_gid));
                }
            }
        }

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
                        return Err(err);
                    }

                    return Ok((*grp).gr_gid);
                }
            }
        }

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
    }

    Ok(())
}
