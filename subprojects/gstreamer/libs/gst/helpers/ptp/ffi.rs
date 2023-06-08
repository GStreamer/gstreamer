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
#[allow(non_camel_case_types, non_upper_case_globals, non_snake_case)]
pub mod unix {
    use std::os::{raw::*, unix::io::RawFd};

    #[cfg(not(any(
        target_os = "linux",
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
        target_os = "solaris",
        target_os = "illumos",
    )))]
    compile_error!("Unsupported Operating System");

    // The API definitions below are taken from the libc crate version 0.2.139, the corresponding C
    // headers, manpages and related documentation.
    //
    // XXX: Once meson has cargo subproject support all of the below can be replaced with the libc crate.
    pub const STDIN_FILENO: RawFd = 0;
    pub const STDOUT_FILENO: RawFd = 1;
    #[cfg(not(test))]
    pub const STDERR_FILENO: RawFd = 2;
    pub const O_RDONLY: c_int = 0;

    pub const POLLIN: c_short = 0x1;
    pub const POLLERR: c_short = 0x8;
    pub const POLLHUP: c_short = 0x10;
    pub const POLLNVAL: c_short = 0x20;

    pub const IPPROTO_IP: c_int = 0;
    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const IP_ADD_MEMBERSHIP: c_int = 12;
    #[cfg(target_os = "linux")]
    pub const IP_ADD_MEMBERSHIP: c_int = 35;
    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const IP_ADD_MEMBERSHIP: c_int = 19;

    #[cfg(any(
        target_os = "solaris",
        target_os = "illumos",
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const SOL_SOCKET: c_int = 0xffff;
    #[cfg(all(
        target_os = "linux",
        any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64",
        )
    ))]
    pub const SOL_SOCKET: c_int = 0xffff;
    #[cfg(all(
        target_os = "linux",
        not(any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64",
        )),
    ))]
    pub const SOL_SOCKET: c_int = 1;

    #[cfg(target_os = "macos")]
    pub const FIOCLEX: c_ulong = 0x20006601;

    #[cfg(target_os = "macos")]
    pub const SO_NOSIGPIPE: c_int = 0x1022;

    #[cfg(any(
        target_os = "solaris",
        target_os = "illumos",
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const SO_REUSEADDR: c_int = 0x4;
    #[cfg(all(
        target_os = "linux",
        any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64",
        ),
    ))]
    pub const SO_REUSEADDR: c_int = 0x4;
    #[cfg(all(
        target_os = "linux",
        not(any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64",
        )),
    ))]
    pub const SO_REUSEADDR: c_int = 2;

    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const SO_REUSEPORT: c_int = 0x200;
    #[cfg(all(
        target_os = "linux",
        any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64",
        ),
    ))]
    pub const SO_REUSEPORT: c_int = 0x200;
    #[cfg(all(
        target_os = "linux",
        not(any(
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "mips",
            target_arch = "mips64"
        )),
    ))]
    pub const SO_REUSEPORT: c_int = 15;

    #[cfg(any(target_os = "freebsd", target_os = "dragonfly", target_os = "netbsd"))]
    pub const SOCK_CLOEXEC: c_int = 0x10000000;
    #[cfg(target_os = "openbsd")]
    pub const SOCK_CLOEXEC: c_int = 0x8000;
    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const SOCK_CLOEXEC: c_int = 0x080000;
    #[cfg(all(
        target_os = "linux",
        any(target_arch = "sparc", target_arch = "sparc64"),
    ))]
    pub const SOCK_CLOEXEC: c_int = 0x400000;
    #[cfg(all(
        target_os = "linux",
        not(any(target_arch = "sparc", target_arch = "sparc64")),
    ))]
    pub const SOCK_CLOEXEC: c_int = 0x80000;

    #[cfg(any(
        target_os = "freebsd",
        target_os = "dragonfly",
        target_os = "netbsd",
        target_os = "openbsd",
        target_os = "macos",
    ))]
    pub const SOCK_DGRAM: c_int = 2;
    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const SOCK_DGRAM: c_int = 1;
    #[cfg(all(target_os = "linux", any(target_arch = "mips", target_arch = "mips64"),))]
    pub const SOCK_DGRAM: c_int = 1;
    #[cfg(all(
        target_os = "linux",
        not(any(target_arch = "mips", target_arch = "mips64")),
    ))]
    pub const SOCK_DGRAM: c_int = 2;

    pub const AF_INET: c_int = 2;
    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const AF_LINK: c_int = 18;
    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const AF_LINK: c_int = 25;
    #[cfg(target_os = "linux")]
    pub const AF_PACKET: c_int = 17;

    pub const IFF_UP: c_int = 0x1;
    pub const IFF_LOOPBACK: c_int = 0x8;

    #[cfg(target_os = "linux")]
    pub const IFF_MULTICAST: c_int = 0x1000;
    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const IFF_MULTICAST: ::c_int = 0x0800;
    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    pub const IFF_MULTICAST: c_int = 0x08000;

    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    pub const IF_NAMESIZE: usize = 32;
    #[cfg(not(any(target_os = "linux", target_os = "solaris", target_os = "illumos")))]
    pub const IF_NAMESIZE: usize = 16;

    pub const PRIO_PROCESS: c_int = 0;

    extern "C" {
        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "open$UNIX2003"
        )]
        pub fn open(path: *const u8, oflag: c_int, ...) -> i32;
        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "read$UNIX2003"
        )]
        pub fn read(fd: RawFd, buf: *mut u8, count: usize) -> isize;
        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "write$UNIX2003"
        )]
        pub fn write(fd: RawFd, buf: *const u8, count: usize) -> isize;

        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "close$UNIX2003"
        )]
        pub fn close(fd: c_int) -> c_int;

        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "poll$UNIX2003"
        )]
        pub fn poll(fds: *mut pollfd, nfds: nfds_t, timeout: c_int) -> c_int;

        #[cfg(not(any(target_os = "solaris", target_os = "illumos")))]
        pub fn if_nametoindex(name: *const c_char) -> c_uint;
        #[cfg(not(any(target_os = "solaris", target_os = "illumos")))]
        pub fn setsockopt(
            socket: c_int,
            level: c_int,
            name: c_int,
            value: *const c_void,
            option_len: u32,
        ) -> c_int;

        pub fn getifaddrs(ifap: *mut *mut ifaddrs) -> c_int;
        pub fn freeifaddrs(ifa: *mut ifaddrs);

        pub fn setpriority(which: c_int, who: c_int, prio: c_int) -> c_int;

        #[cfg_attr(target_os = "netbsd", link_name = "__socket30")]
        #[cfg_attr(target_os = "illumos", link_name = "__xnet_socket")]
        pub fn socket(domain: c_int, ty: c_int, protocol: c_int) -> c_int;

        #[cfg_attr(target_os = "illumos", link_name = "__xnet_bind")]
        #[cfg_attr(
            all(target_os = "macos", target_arch = "x86"),
            link_name = "bind$UNIX2003"
        )]
        pub fn bind(socket: c_int, address: *const sockaddr, address_len: u32) -> c_int;

        #[cfg(target_os = "macos")]
        pub fn ioctl(fd: c_int, request: c_ulong, ...) -> c_int;

        #[cfg(test)]
        pub fn pipe(pipefd: *mut i32) -> i32;
    }

    #[cfg(any(target_os = "linux", target_os = "solaris", target_os = "illumos"))]
    pub type nfds_t = c_ulong;
    #[cfg(not(any(target_os = "linux", target_os = "solaris", target_os = "illumos")))]
    pub type nfds_t = c_uint;

    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct pollfd {
        pub fd: c_int,
        pub events: c_short,
        pub revents: c_short,
    }

    pub type in_addr_t = u32;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct in_addr {
        pub s_addr: in_addr_t,
    }

    // Solaris does not have support for this so we fall back to use the std API
    #[cfg(not(any(target_os = "solaris", target_os = "illumos")))]
    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct ip_mreqn {
        pub imr_multiaddr: in_addr,
        pub imr_address: in_addr,
        pub imr_ifindex: c_int,
    }

    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    #[repr(C)]
    pub struct ifaddrs {
        pub ifa_next: *mut ifaddrs,
        pub ifa_name: *mut c_char,
        pub ifa_flags: c_ulong,
        pub ifa_addr: *mut sockaddr,
        pub ifa_netmask: *mut sockaddr,
        pub ifa_dstaddr: *mut sockaddr,
        pub ifa_data: *mut c_void,
    }

    #[cfg(target_os = "linux")]
    #[repr(C)]
    pub struct ifaddrs {
        pub ifa_next: *mut ifaddrs,
        pub ifa_name: *mut c_char,
        pub ifa_flags: c_uint,
        pub ifa_addr: *mut sockaddr,
        pub ifa_netmask: *mut sockaddr,
        pub ifa_ifu: *mut sockaddr,
        pub ifa_data: *mut c_void,
    }

    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    #[repr(C)]
    pub struct ifaddrs {
        pub ifa_next: *mut ifaddrs,
        pub ifa_name: *mut c_char,
        pub ifa_flags: c_uint,
        pub ifa_addr: *mut sockaddr,
        pub ifa_netmask: *mut sockaddr,
        pub ifa_dstaddr: *mut sockaddr,
        pub ifa_data: *mut c_void,
        #[cfg(target_os = "netbsd")]
        pub ifa_addrflags: c_uint,
    }

    #[cfg(any(target_os = "linux", target_os = "solaris", target_os = "illumos"))]
    #[repr(C)]
    pub struct sockaddr {
        pub sa_family: u16,
        pub sa_data: [u8; 14],
    }

    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    #[repr(C)]
    pub struct sockaddr {
        pub sa_len: u8,
        pub sa_family: u8,
        pub sa_data: [u8; 14],
    }

    #[cfg(any(target_os = "linux", target_os = "solaris", target_os = "illumos"))]
    #[repr(C)]
    pub struct sockaddr_in {
        pub sin_family: u16,
        pub sin_port: u16,
        pub sin_addr: in_addr,
        pub sin_zero: [u8; 8],
    }
    #[cfg(any(
        target_os = "freebsd",
        target_os = "openbsd",
        target_os = "netbsd",
        target_os = "dragonfly",
        target_os = "macos",
    ))]
    #[repr(C)]
    pub struct sockaddr_in {
        pub sin_len: u8,
        pub sin_family: u8,
        pub sin_port: u16,
        pub sin_addr: in_addr,
        pub sin_zero: [u8; 8],
    }

    #[cfg(any(target_os = "solaris", target_os = "illumos"))]
    #[repr(C)]
    pub struct sockaddr_dl {
        pub sdl_family: u16,
        pub sdl_index: u16,
        pub sdl_type: u8,
        pub sdl_nlen: u8,
        pub sdl_alen: u8,
        pub sdl_slen: u8,
        pub sdl_data: [u8; 244],
    }

    #[cfg(any(target_os = "netbsd", target_os = "macos"))]
    #[repr(C)]
    pub struct sockaddr_dl {
        pub sdl_len: u8,
        pub sdl_family: u8,
        pub sdl_index: u16,
        pub sdl_type: u8,
        pub sdl_nlen: u8,
        pub sdl_alen: u8,
        pub sdl_slen: u8,
        pub sdl_data: [u8; 12],
    }

    #[cfg(target_os = "openbsd")]
    #[repr(C)]
    pub struct sockaddr_dl {
        pub sdl_len: u8,
        pub sdl_family: u8,
        pub sdl_index: u16,
        pub sdl_type: u8,
        pub sdl_nlen: u8,
        pub sdl_alen: u8,
        pub sdl_slen: u8,
        pub sdl_data: [u8; 24],
    }

    #[cfg(target_os = "freebsd")]
    #[repr(C)]
    pub struct sockaddr_dl {
        pub sdl_len: u8,
        pub sdl_family: u8,
        pub sdl_index: u16,
        pub sdl_type: u8,
        pub sdl_nlen: u8,
        pub sdl_alen: u8,
        pub sdl_slen: u8,
        pub sdl_data: [u8; 46],
    }

    #[cfg(target_os = "dragonfly")]
    #[repr(C)]
    pub struct sockaddr_dl {
        pub sdl_len: u8,
        pub sdl_family: u8,
        pub sdl_index: u16,
        pub sdl_type: u8,
        pub sdl_nlen: u8,
        pub sdl_alen: u8,
        pub sdl_slen: u8,
        pub sdl_data: [u8; 12],
        pub sdl_rcf: u16,
        pub sdl_route: [u8; 16],
    }

    #[cfg(target_os = "linux")]
    #[repr(C)]
    pub struct sockaddr_ll {
        pub sll_family: u16,
        pub sll_protocol: u16,
        pub sll_ifindex: u32,
        pub sll_hatype: u16,
        pub sll_pkttype: u8,
        pub sll_halen: u8,
        pub sll_addr: [u8; 8],
    }

    #[cfg(target_os = "linux")]
    pub mod linux {
        pub use super::*;

        #[cfg(target_arch = "x86")]
        pub const SYS_getrandom: c_ulong = 355;
        #[cfg(all(target_arch = "x86_64", target_pointer_width = "32"))]
        pub const SYS_getrandom: c_ulong = 0x40000000 + 318;
        #[cfg(all(target_arch = "x86_64", target_pointer_width = "64"))]
        pub const SYS_getrandom: c_ulong = 318;
        #[cfg(target_arch = "arm")]
        pub const SYS_getrandom: c_ulong = 384;
        #[cfg(target_arch = "aarch64")]
        pub const SYS_getrandom: c_ulong = 278;
        #[cfg(target_arch = "mips")]
        pub const SYS_getrandom: c_ulong = 4000 + 353;
        #[cfg(target_arch = "mips64")]
        pub const SYS_getrandom: c_ulong = 5000 + 313;
        #[cfg(any(
            target_arch = "riscv32",
            target_arch = "riscv64",
            target_arch = "loongarch64"
        ))]
        pub const SYS_getrandom: c_ulong = 278;
        #[cfg(any(target_arch = "powerpc", target_arch = "powerpc64"))]
        pub const SYS_getrandom: c_ulong = 359;
        #[cfg(any(target_arch = "sparc", target_arch = "sparc64"))]
        pub const SYS_getrandom: c_ulong = 347;
        #[cfg(target_arch = "s390x")]
        pub const SYS_getrandom: c_ulong = 349;
        #[cfg(target_arch = "m68k")]
        pub const SYS_getrandom: c_ulong = 352;
        #[cfg(not(any(
            target_arch = "x86",
            target_arch = "x86_64",
            target_arch = "arm",
            target_arch = "aarch64",
            target_arch = "mips",
            target_arch = "mips64",
            target_arch = "riscv32",
            target_arch = "riscv64",
            target_arch = "loongarch64",
            target_arch = "powerpc",
            target_arch = "powerpc64",
            target_arch = "sparc",
            target_arch = "sparc64",
            target_arch = "s390x",
            target_arch = "m68k",
        )))]
        pub const SYS_getrandom: c_ulong = 0;

        extern "C" {
            pub fn syscall(num: c_ulong, ...) -> c_long;
        }
    }

    #[cfg(target_os = "macos")]
    pub mod macos {
        pub use super::*;

        #[repr(C)]
        pub struct mach_timebase_info {
            pub numer: u32,
            pub denom: u32,
        }

        extern "C" {
            pub fn mach_timebase_info(info: *mut mach_timebase_info) -> c_int;
            pub fn mach_absolute_time() -> u64;
        }
    }

    #[cfg(not(target_os = "macos"))]
    pub mod clock_gettime {
        pub use super::*;

        #[cfg(any(
            target_os = "linux",
            target_os = "freebsd",
            target_os = "openbsd",
            target_os = "netbsd",
            target_os = "solaris",
            target_os = "illumos",
        ))]
        pub type clock_id_t = c_int;

        #[cfg(target_os = "dragonfly")]
        pub type clock_id_t = c_ulong;

        #[cfg(any(target_os = "solaris", target_os = "illumos"))]
        pub type time_t = c_long;

        #[cfg(any(target_os = "openbsd", target_os = "netbsd", target_os = "dragonfly"))]
        pub type time_t = i64;

        #[cfg(all(target_os = "freebsd", target_arch = "x86"))]
        pub type time_t = i32;
        #[cfg(all(target_os = "freebsd", not(target_arch = "x86")))]
        pub type time_t = i64;

        #[cfg(all(target_os = "linux", target_env = "gnu", target_arch = "riscv32"))]
        pub type time_t = i64;
        #[cfg(all(
            target_os = "linux",
            target_env = "gnu",
            any(
                target_arch = "x86",
                target_arch = "arm",
                target_arch = "m68k",
                target_arch = "mips",
                target_arch = "powerpc",
                target_arch = "sparc",
                all(target_arch = "aarch64", target_pointer_width = "32"),
            )
        ))]
        pub type time_t = i32;
        #[cfg(all(
            target_os = "linux",
            target_env = "gnu",
            any(
                target_arch = "x86_64",
                all(target_arch = "aarch64", target_pointer_width = "64"),
                target_arch = "powerpc64",
                target_arch = "mips64",
                target_arch = "s390x",
                target_arch = "sparc64",
                target_arch = "riscv64",
                target_arch = "loongarch64",
            )
        ))]
        pub type time_t = i64;
        #[cfg(all(target_os = "linux", target_env = "musl"))]
        pub type time_t = c_long;

        #[cfg(all(target_os = "linux", target_env = "uclibc", target_arch = "x86_64"))]
        pub type time_t = c_int;
        #[cfg(all(
            target_os = "linux",
            target_env = "uclibc",
            not(target_arch = "x86_64"),
        ))]
        pub type time_t = c_long;

        #[repr(C)]
        pub struct timespec {
            pub tv_sec: time_t,
            #[cfg(all(target_arch = "x86_64", target_pointer_width = "32"))]
            pub tv_nsec: i64,
            #[cfg(not(all(target_arch = "x86_64", target_pointer_width = "32")))]
            pub tv_nsec: c_long,
        }

        #[cfg(any(
            target_os = "freebsd",
            target_os = "dragonfly",
            target_os = "solaris",
            target_os = "illumos",
        ))]
        pub const CLOCK_MONOTONIC: clock_id_t = 4;

        #[cfg(any(target_os = "openbsd", target_os = "netbsd",))]
        pub const CLOCK_MONOTONIC: clock_id_t = 3;

        #[cfg(target_os = "linux")]
        pub const CLOCK_MONOTONIC: clock_id_t = 1;

        extern "C" {
            pub fn clock_gettime(clk_id: clock_id_t, tp: *mut timespec) -> c_int;
        }
    }

    #[cfg(ptp_helper_permissions = "setcap")]
    pub mod setcaps {
        use super::*;

        pub type cap_t = *mut c_void;

        #[link(name = "cap")]
        extern "C" {
            pub fn cap_clear(c: cap_t) -> c_int;
            pub fn cap_get_proc() -> cap_t;
            pub fn cap_set_proc(c: cap_t) -> c_int;
            pub fn cap_free(c: cap_t) -> c_int;
        }
    }

    #[cfg(ptp_helper_permissions = "setuid-root")]
    pub mod setuid_root {
        use super::*;

        pub type uid_t = u32;
        pub type gid_t = u32;

        #[repr(C)]
        pub struct passwd {
            pub pw_name: *mut c_char,
            pub pw_passwd: *mut c_char,
            pub pw_uid: uid_t,
            pub pw_gid: gid_t,
            // More fields following here
            truncated: c_void,
        }

        #[repr(C)]
        pub struct group {
            pub gr_name: *mut c_char,
            pub gr_passwd: *mut c_char,
            pub gr_gid: gid_t,
            // More fields following here
            truncated: c_void,
        }

        extern "C" {
            #[cfg_attr(target_os = "netbsd", link_name = "__getpwnam50")]
            pub fn getpwnam(name: *const c_char) -> *mut passwd;
            pub fn getgrnam(name: *const c_char) -> *mut group;
            pub fn setgid(gid: gid_t) -> c_int;
            pub fn getgid() -> gid_t;
            pub fn setuid(gid: gid_t) -> c_int;
        }
    }
}

#[cfg(windows)]
#[allow(non_camel_case_types, non_upper_case_globals, non_snake_case)]
pub mod windows {
    use std::os::{
        raw::*,
        windows::raw::{HANDLE, SOCKET},
    };

    // The API definitions below are taken from the windows-sys crate version 0.45.0, the
    // corresponding C headers, MSDN and related documentation.
    //
    // XXX: Once meson has cargo subproject support all of the below can be replaced with the windows-sys crate.
    pub const INVALID_HANDLE_VALUE: HANDLE = (-1 as isize as usize) as HANDLE;
    pub const INVALID_SOCKET: SOCKET = (-1 as isize as usize) as SOCKET;

    pub const STD_INPUT_HANDLE: i32 = -10;
    pub const STD_OUTPUT_HANDLE: i32 = -11;
    #[cfg(not(test))]
    pub const STD_ERROR_HANDLE: i32 = -12;

    pub const FILE_TYPE_CHAR: u32 = 0x0002;
    pub const FILE_TYPE_PIPE: u32 = 0x0003;

    pub const THREAD_PRIORITY_TIME_CRITICAL: i32 = 15;

    #[link(name = "kernel32")]
    extern "system" {
        pub fn GetStdHandle(nstdhandle: i32) -> HANDLE;

        pub fn ReadFile(
            hfile: HANDLE,
            lpbuffer: *mut u8,
            nnumberofbytestoread: u32,
            lpnumberofbytesread: *mut u32,
            lpoverlapped: *mut c_void,
        ) -> i32;

        pub fn WriteFile(
            hfile: HANDLE,
            lpbuffer: *const u8,
            nnumberofbytestowrite: u32,
            lpnumberofbyteswritten: *mut u32,
            lpoverlapped: *mut c_void,
        ) -> i32;

        pub fn WaitForMultipleObjects(
            ncount: u32,
            lphandles: *const HANDLE,
            bwaitall: i32,
            dwmilliseconds: u32,
        ) -> u32;

        pub fn SetConsoleMode(hconsolehandle: HANDLE, dwmode: u32) -> i32;
        pub fn FlushConsoleInputBuffer(hconsolehandle: HANDLE) -> i32;

        pub fn CreateEventA(
            lpeventattributes: *const c_void,
            bmanualreset: i32,
            binitialstate: i32,
            lpname: *const u8,
        ) -> HANDLE;
        pub fn SetEvent(hevent: HANDLE) -> i32;
        pub fn ResetEvent(hevent: HANDLE) -> i32;
        pub fn CloseHandle(hobject: HANDLE) -> i32;

        pub fn GetFileType(hfile: HANDLE) -> u32;

        pub fn GetProcessHeap() -> isize;
        pub fn HeapAlloc(hheap: isize, dwflags: u32, dwbytes: usize) -> *mut c_void;
        pub fn HeapFree(hheap: isize, dwflags: u32, lpmem: *mut c_void) -> i32;
        pub fn HeapReAlloc(
            hheap: isize,
            dwflags: u32,
            lpmem: *mut c_void,
            dwbytes: usize,
        ) -> *mut c_void;

        pub fn SetThreadPriority(pthread: HANDLE, npriority: i32) -> i32;
        pub fn GetCurrentThread() -> HANDLE;

        #[cfg(test)]
        pub fn CreatePipe(
            hreadpipe: *mut HANDLE,
            hwritepipe: *mut HANDLE,
            lppipeattributes: *mut c_void,
            nsize: u32,
        ) -> i32;

        pub fn QueryPerformanceFrequency(lpfrequence: *mut i64) -> i32;
        pub fn QueryPerformanceCounter(lpperformancecount: *mut i64) -> i32;
    }

    pub const BCRYPT_USE_SYSTEM_PREFERRED_RNG: u32 = 0x00000002;

    #[link(name = "bcrypt")]
    extern "system" {
        pub fn BCryptGenRandom(
            hAlgorithm: *mut c_void,
            pBuffer: *mut u8,
            cbBuffer: u32,
            dwFlags: u32,
        ) -> u32;
    }

    pub const FD_READ: u32 = 1;
    pub const FD_READ_BIT: usize = 0;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct WSANETWORKEVENTS {
        pub lnetworkevents: u32,
        pub ierrorcode: [i32; 10],
    }

    pub const IPPROTO_IP: u32 = 0u32;
    pub const IP_ADD_MEMBERSHIP: u32 = 12u32;

    pub const SOL_SOCKET: u32 = 65535;
    pub const SO_REUSEADDR: u32 = 4;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IN_ADDR_0_0 {
        pub s_b1: u8,
        pub s_b2: u8,
        pub s_b3: u8,
        pub s_b4: u8,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IN_ADDR_0_1 {
        pub s_w1: u16,
        pub s_w2: u16,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub union IN_ADDR_0 {
        pub S_un_b: IN_ADDR_0_0,
        pub S_un_w: IN_ADDR_0_1,
        pub S_addr: u32,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IN_ADDR {
        pub S_un: IN_ADDR_0,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IP_MREQ {
        pub imr_multiaddr: IN_ADDR,
        pub imr_address: IN_ADDR,
    }

    #[link(name = "ws2_32")]
    extern "system" {
        pub fn WSAEventSelect(s: SOCKET, heventobject: HANDLE, lnetworkevents: u32) -> i32;
        pub fn WSAEnumNetworkEvents(
            s: SOCKET,
            heventobject: HANDLE,
            lpnetworkevents: *mut WSANETWORKEVENTS,
        ) -> i32;
        pub fn WSACreateEvent() -> HANDLE;
        pub fn WSACloseEvent(hevent: HANDLE) -> i32;
        pub fn WSAGetLastError() -> i32;

        pub fn setsockopt(
            socket: SOCKET,
            level: i32,
            name: i32,
            value: *const c_void,
            option_len: i32,
        ) -> i32;

        pub fn WSASocketW(
            af: i32,
            ty: i32,
            protocol: i32,
            lpprotocolinfo: *const c_void,
            g: u32,
            dwflags: u32,
        ) -> SOCKET;
        pub fn bind(s: SOCKET, name: *const SOCKADDR, namelen: i32) -> i32;
        pub fn closesocket(socket: SOCKET) -> i32;
    }

    pub const AF_INET: u32 = 2;

    pub const SOCK_DGRAM: u16 = 2u16;

    pub const WSA_FLAG_OVERLAPPED: u32 = 1u32;
    pub const WSA_FLAG_NO_HANDLE_INHERIT: u32 = 128u32;

    pub const GAA_FLAG_SKIP_ANYCAST: u32 = 0x0002;
    pub const GAA_FLAG_SKIP_MULTICAST: u32 = 0x0004;
    pub const GAA_FLAG_SKIP_DNS_SERVER: u32 = 0x0008;

    pub const ADAPTER_FLAG_RECEIVE_ONLY: u32 = 0x08;
    pub const ADAPTER_FLAG_NO_MULTICAST: u32 = 0x10;
    pub const ADAPTER_FLAG_IPV4_ENABLED: u32 = 0x80;

    pub const IF_TYPE_SOFTWARE_LOOPBACK: u32 = 24;

    pub const IF_OPER_STATUS_UP: u32 = 1;

    pub const ERROR_NOT_ENOUGH_MEMORY: u32 = 8u32;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IP_ADAPTER_ADDRESSES_LH_0_0 {
        pub length: u32,
        pub ifindex: u32,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub union IP_ADAPTER_ADDRESSES_LH_0 {
        pub alignment: u64,
        pub anonymous: IP_ADAPTER_ADDRESSES_LH_0_0,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct IP_ADAPTER_UNICAST_ADDRESS_LH_0_0 {
        pub length: u32,
        pub ifindex: u32,
    }

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub union IP_ADAPTER_UNICAST_ADDRESS_LH_0 {
        pub alignment: u64,
        pub anonymous: IP_ADAPTER_UNICAST_ADDRESS_LH_0_0,
    }

    // XXX: Actually SOCKADDR_IN but we don't care about others
    #[repr(C)]
    pub struct SOCKADDR {
        pub sa_family: u16,
        pub sin_port: u16,
        pub in_addr: IN_ADDR,
        pub sin_zero: [u8; 8],
    }

    #[repr(C)]
    pub struct SOCKET_ADDRESS {
        pub lpsocketaddr: *mut SOCKADDR,
        pub isocketaddrlength: i32,
    }

    #[repr(C)]
    pub struct IP_ADAPTER_UNICAST_ADDRESS_LH {
        pub anonymous: IP_ADAPTER_UNICAST_ADDRESS_LH_0,
        pub next: *mut IP_ADAPTER_UNICAST_ADDRESS_LH,
        pub address: SOCKET_ADDRESS,
        // More fields following here
        truncated: c_void,
    }

    #[repr(C)]
    pub struct IP_ADAPTER_ADDRESSES_LH {
        pub anonymous: IP_ADAPTER_ADDRESSES_LH_0,
        pub next: *mut IP_ADAPTER_ADDRESSES_LH,
        pub adaptername: *const u8,
        pub firstunicastaddress: *mut IP_ADAPTER_UNICAST_ADDRESS_LH,
        pub firstanycastaddress: *mut c_void,
        pub firstmulticastaddress: *mut c_void,
        pub firstdnsserveraddress: *mut c_void,
        pub dnssuffix: *const u16,
        pub description: *const u16,
        pub friendlyname: *const u16,
        pub physicaladdress: [u8; 8],
        pub physicaladdresslength: u32,
        pub flags: u32,
        pub mtu: u32,
        pub iftype: u32,
        pub operstatus: u32,
        // More fields following here
        truncated: c_void,
    }

    #[link(name = "iphlpapi")]
    extern "system" {
        pub fn GetAdaptersAddresses(
            family: u32,
            flags: u32,
            reserved: *mut c_void,
            adapteraddresses: *mut IP_ADAPTER_ADDRESSES_LH,
            sizepointer: *mut u32,
        ) -> u32;
    }
}
