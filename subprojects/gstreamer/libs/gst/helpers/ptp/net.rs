// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use std::net::Ipv4Addr;

use crate::{bail, error::Error};

#[derive(Debug)]
/// Network interface information.
pub struct InterfaceInfo {
    /// Name of the interface
    pub name: String,
    /// Other name of the interface, if any
    pub other_name: Option<String>,
    /// Interface index
    pub index: usize,
    /// Unicast IPv4 address of the interface
    pub ip_addr: Ipv4Addr,
    /// Physical MAC address of the interface, if any
    pub hw_addr: Option<[u8; 6]>,
}

#[cfg(unix)]
mod imp {
    use super::*;

    use std::{ffi::CStr, io, marker, mem, net::UdpSocket, os::unix::io::AsRawFd, ptr};

    use crate::{error::Context, ffi::unix::*};

    /// Returns information for all non-loopback, multicast-capable network interfaces.
    pub fn query_interfaces() -> Result<Vec<InterfaceInfo>, Error> {
        struct Ifaddrs(*mut ifaddrs);

        impl Ifaddrs {
            fn new() -> io::Result<Self> {
                loop {
                    // SAFETY: Requires passing valid storage for the returned ifaddrs pointer and
                    // returns -1 on errors. It might return NULL if there are no network interfaces.
                    //
                    // On error it might return EINTR in which case we should simply try again.
                    unsafe {
                        let mut ifaddrs = ptr::null_mut();
                        if getifaddrs(&mut ifaddrs) == -1 {
                            let err = io::Error::last_os_error();
                            if err.kind() == std::io::ErrorKind::Interrupted {
                                continue;
                            }

                            return Err(err);
                        } else {
                            return Ok(Self(ifaddrs));
                        }
                    }
                }
            }

            fn iter(&self) -> IfaddrsIter {
                IfaddrsIter {
                    ptr: ptr::NonNull::new(self.0),
                    phantom: marker::PhantomData,
                }
            }
        }

        impl Drop for Ifaddrs {
            fn drop(&mut self) {
                // SAFETY: The pointer is a valid ifaddrs pointer by construction and dropped only
                // once, so freeing it here is OK. It might be NULL so check for that first.
                unsafe {
                    if !self.0.is_null() {
                        freeifaddrs(self.0);
                    }
                }
            }
        }

        struct IfaddrsIter<'a> {
            ptr: Option<ptr::NonNull<ifaddrs>>,
            phantom: marker::PhantomData<&'a Ifaddrs>,
        }

        impl<'a> Iterator for IfaddrsIter<'a> {
            type Item = &'a ifaddrs;

            fn next(&mut self) -> Option<Self::Item> {
                match self.ptr {
                    None => None,
                    Some(ptr) => {
                        // SAFETY: The pointer is a valid ifaddrs pointer by construction so
                        // creating a reference to it is OK.
                        let addr = unsafe { &*ptr.as_ptr() };
                        self.ptr = ptr::NonNull::new(addr.ifa_next);
                        Some(addr)
                    }
                }
            }
        }

        let ifaddrs = Ifaddrs::new().context("Failed getting interface addresses")?;

        let mut if_infos = Vec::<InterfaceInfo>::new();

        for ifaddr in ifaddrs.iter() {
            // SAFETY: ifa_name points to a NUL-terminated interface name string that is valid as
            // long as its struct is
            let name = unsafe { CStr::from_ptr(ifaddr.ifa_name) }.to_str().unwrap();

            // Skip loopback interfaces, interfaces that are not up and interfaces that can't do
            // multicast. These are all unusable for PTP purposes.
            let flags = ifaddr.ifa_flags;
            if flags & IFF_LOOPBACK as u32 != 0 {
                debug!("Interface {} is loopback interface", name);
                continue;
            }
            if flags & IFF_UP as u32 == 0 {
                debug!("Interface {} is not up", name);
                continue;
            }
            if flags & IFF_MULTICAST as u32 == 0 {
                debug!("Interface {} does not support multicast", name);
                continue;
            }

            // If the interface has no address then skip it. Only interfaces with IPv4 addresses
            // are usable for PTP purposes.
            if ifaddr.ifa_addr.is_null() {
                debug!("Interface {} has no IPv4 address", name);
                continue;
            }

            // Get the interface index from its name. If it has none then we can't use it to join
            // the PTP multicast group reliable for this interface.
            //
            // SAFETY: Must be called with a valid, NUL-terminated string which is provided by
            // ifa_name and will return the interface index or zero on error.
            //
            // On error it can return EINTR in which case we should try again.
            let index = loop {
                let index = unsafe { if_nametoindex(ifaddr.ifa_name) } as usize;
                if index == 0 {
                    let err = io::Error::last_os_error();
                    if err.kind() == io::ErrorKind::Interrupted {
                        continue;
                    }
                }
                break index;
            };
            if index == 0 {
                debug!("Interface {} has no valid interface index", name);
                continue;
            }

            // Interfaces are listed multiple times here, once per address. We collect all IPv4 and
            // MAC addresses for interfaces below.

            // SAFETY: ifa_addr is a valid sockaddr pointer and was checked to be not NULL further
            // above.
            let sa_family = unsafe { (*ifaddr.ifa_addr).sa_family };

            // If this interface has an IPv4 address then retrieve and store it here.
            if sa_family == AF_INET as _ {
                // SAFETY: If the address family is AF_INET then it is actually a valid sockaddr_in
                // pointer and can be used as such.
                let addr = unsafe { &*(ifaddr.ifa_addr as *const sockaddr_in) };
                let ip_addr = Ipv4Addr::from(addr.sin_addr.s_addr.to_ne_bytes());

                debug!("Interface {} has IPv4 address {}", name, ip_addr);

                if let Some(if_info) = if_infos.iter_mut().find(|info| info.name == name) {
                    if if_info.ip_addr.is_broadcast() {
                        if_info.ip_addr = ip_addr;
                    }
                } else {
                    if_infos.push(InterfaceInfo {
                        name: String::from(name),
                        other_name: None,
                        index,
                        ip_addr,
                        hw_addr: None,
                    });
                }
            }

            #[cfg(target_os = "linux")]
            {
                if sa_family == AF_PACKET as _ {
                    // SAFETY: If the address family is AF_PACKET then it is actually a valid sockaddr_ll
                    // pointer and can be used as such.
                    let addr = unsafe { &*(ifaddr.ifa_addr as *const sockaddr_ll) };
                    if addr.sll_halen == 6 {
                        let mut hw_addr = [0u8; 6];
                        hw_addr.copy_from_slice(&addr.sll_addr[0..6]);

                        debug!("Interface {} has MAC address {:?}", name, hw_addr);

                        if let Some(if_info) = if_infos.iter_mut().find(|info| info.name == name) {
                            if if_info.hw_addr.is_none() {
                                if_info.hw_addr = Some(hw_addr);
                            }
                        } else {
                            if_infos.push(InterfaceInfo {
                                name: String::from(name),
                                other_name: None,
                                index,
                                ip_addr: Ipv4Addr::BROADCAST,
                                hw_addr: Some(hw_addr),
                            });
                        }
                    }
                }
            }
            #[cfg(not(target_os = "linux"))]
            {
                use std::slice;

                if sa_family == AF_LINK as _ {
                    // SAFETY: If the address family is AF_LINK then it is actually a valid sockaddr_dl
                    // pointer and can be used as such.
                    let addr = unsafe { &*(ifaddr.ifa_addr as *const sockaddr_dl) };
                    if addr.sdl_nlen <= IF_NAMESIZE as u8 && addr.sdl_alen == 6 {
                        let mut hw_addr = [0u8; 6];
                        // SAFETY: There can be more than the given number of bytes stored and
                        // this happens regularly on macOS at least. It is required that the
                        // interface name is at most IF_NAMESIZE (checked just above).
                        unsafe {
                            let sdl_addr_ptr = addr.sdl_data.as_ptr() as *const u8;
                            let sdl_addr =
                                slice::from_raw_parts(sdl_addr_ptr.add(addr.sdl_nlen as usize), 6);
                            hw_addr.copy_from_slice(sdl_addr);
                        }

                        debug!("Interface {} has MAC address {:?}", name, hw_addr);

                        if let Some(if_info) = if_infos.iter_mut().find(|info| info.name == name) {
                            if if_info.hw_addr.is_none() {
                                if_info.hw_addr = Some(hw_addr);
                            }
                        } else {
                            if_infos.push(InterfaceInfo {
                                name: String::from(name),
                                other_name: None,
                                index,
                                ip_addr: Ipv4Addr::BROADCAST,
                                hw_addr: Some(hw_addr),
                            });
                        }
                    }
                }
            }
        }

        if_infos.retain(|iface| !iface.ip_addr.is_broadcast());

        Ok(if_infos)
    }

    /// Create an `UdpSocket` and bind it to the given address but set `SO_REUSEADDR` and/or
    /// `SO_REUSEPORT` before doing so.
    ///
    /// `UdpSocket::bind()` does not allow setting custom options before binding.
    pub fn create_udp_socket(addr: &Ipv4Addr, port: u16) -> Result<UdpSocket, io::Error> {
        use std::os::unix::io::FromRawFd;

        /// Helper struct to keep a raw fd and close it on drop
        struct Fd(i32);
        impl Drop for Fd {
            fn drop(&mut self) {
                unsafe {
                    // SAFETY: The integer is a valid fd by construction.
                    let _ = close(self.0);
                }
            }
        }

        // SAFETY: Calling socket() is safe at any time and will simply fail if invalid parameters
        // are passed.
        let fd = unsafe {
            #[cfg(any(
                target_os = "linux",
                target_os = "freebsd",
                target_os = "openbsd",
                target_os = "netbsd",
                target_os = "dragonfly",
                target_os = "solaris",
                target_os = "illumos",
            ))]
            let ty = SOCK_DGRAM | SOCK_CLOEXEC;
            #[cfg(target_os = "macos")]
            let ty = SOCK_DGRAM;

            let res = socket(AF_INET, ty, 0);
            if res == -1 {
                return Err(io::Error::last_os_error());
            }
            Fd(res)
        };

        // SAFETY: A valid socket fd is passed to ioctl() and setsockopt() and the parameters to
        // setsockopt() are according to the type expected by SO_NOSIGPIPE.
        #[cfg(target_os = "macos")]
        unsafe {
            let res = ioctl(fd.0, FIOCLEX);
            if res == -1 {
                return Err(io::Error::last_os_error());
            }

            let val = 1i32;
            let res = setsockopt(
                fd.0,
                SOL_SOCKET,
                SO_NOSIGPIPE,
                &val as *const _ as *const _,
                mem::size_of_val(&val) as _,
            );
            if res < 0 {
                return Err(io::Error::last_os_error());
            }
        }

        // SAFETY: A valid socket fd is passed here.
        unsafe {
            set_reuse(fd.0);
        }

        // SAFETY: A valid socket fd is passed together with a valid sockaddr_in and its size.
        unsafe {
            let addr = sockaddr_in {
                sin_family: AF_INET as _,
                sin_port: u16::to_be(port),
                sin_addr: in_addr {
                    s_addr: u32::from_ne_bytes(addr.octets()),
                },
                sin_zero: [0u8; 8],
                #[cfg(any(
                    target_os = "freebsd",
                    target_os = "openbsd",
                    target_os = "netbsd",
                    target_os = "dragonfly",
                    target_os = "macos",
                ))]
                sin_len: mem::size_of::<sockaddr_in>() as _,
            };
            let res = bind(
                fd.0,
                &addr as *const _ as *const _,
                mem::size_of_val(&addr) as _,
            );
            if res < 0 {
                return Err(io::Error::last_os_error());
            }
        }

        unsafe { Ok(UdpSocket::from_raw_fd(mem::ManuallyDrop::new(fd).0)) }
    }

    /// Join multicast address for a given interface.
    pub fn join_multicast_v4(
        socket: &UdpSocket,
        addr: &Ipv4Addr,
        iface: &InterfaceInfo,
    ) -> Result<(), Error> {
        #[cfg(not(any(target_os = "solaris", target_os = "illumos")))]
        {
            let mreqn = ip_mreqn {
                imr_multiaddr: in_addr {
                    s_addr: u32::from_ne_bytes(addr.octets()),
                },
                imr_address: in_addr {
                    s_addr: u32::from_ne_bytes(Ipv4Addr::UNSPECIFIED.octets()),
                },
                imr_ifindex: iface.index as _,
            };

            // SAFETY: Requires a valid ip_mreq or ip_mreqn struct to be passed together
            // with its size for checking which of the two it is. On errors a negative
            // integer is returned.
            unsafe {
                if setsockopt(
                    socket.as_raw_fd(),
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    &mreqn as *const _ as *const _,
                    mem::size_of_val(&mreqn) as _,
                ) < 0
                {
                    bail!(
                        source: io::Error::last_os_error(),
                        "Failed joining multicast group for interface {}",
                        iface.name,
                    );
                }
            }

            Ok(())
        }
        #[cfg(any(target_os = "solaris", target_os = "illumos"))]
        {
            use crate::error::Context;

            socket
                .join_multicast_v4(addr, &iface.ip_addr)
                .with_context(|| {
                    format!(
                        "Failed joining multicast group for interface {} at address {}",
                        iface.name, iface.ip_addr
                    )
                })?;

            Ok(())
        }
    }

    /// Allow multiple sockets to bind to the same address / port.
    ///
    /// This is best-effort and might not actually do anything.
    ///
    /// SAFETY: Must be called with a valid socket fd.
    unsafe fn set_reuse(socket: i32) {
        // SAFETY: SO_REUSEADDR takes an i32 value that can be 0/false or 1/true and
        // enables the given feature on the socket.
        //
        // We explicitly ignore errors here. If it works, good, if it doesn't then not much
        // lost other than the ability to run multiple processes at once.
        unsafe {
            let v = 1i32;
            let res = setsockopt(
                socket,
                SOL_SOCKET,
                SO_REUSEADDR,
                &v as *const _ as *const _,
                mem::size_of_val(&v) as u32,
            );
            if res < 0 {
                warn!("Failed to set SO_REUSEADDR on socket");
            }
        }

        #[cfg(not(any(target_os = "solaris", target_os = "illumos")))]
        {
            // SAFETY: SO_REUSEPORT takes an i32 value that can be 0/false or 1/true and
            // enables the given feature on the socket.
            //
            // We explicitly ignore errors here. If it works, good, if it doesn't then not much
            // lost other than the ability to run multiple processes at once.
            unsafe {
                let v = 1i32;
                let res = setsockopt(
                    socket,
                    SOL_SOCKET,
                    SO_REUSEPORT,
                    &v as *const _ as *const _,
                    mem::size_of_val(&v) as u32,
                );

                if res < 0 {
                    warn!("Failed to set SO_REUSEPORT on socket");
                }
            }
        }
    }
}

#[cfg(windows)]
mod imp {
    use super::*;

    use std::{
        ffi::{CStr, OsString},
        io, marker, mem,
        net::{Ipv4Addr, SocketAddr, UdpSocket},
        os::{
            raw::*,
            windows::{ffi::OsStringExt, io::AsRawSocket, raw::SOCKET},
        },
        ptr, slice,
    };

    use crate::{error::Context, ffi::windows::*};

    /// Returns information for all non-loopback, multicast-capable network interfaces.
    pub fn query_interfaces() -> Result<Vec<InterfaceInfo>, Error> {
        struct AdapterAddresses {
            addresses: ptr::NonNull<IP_ADAPTER_ADDRESSES_LH>,
            heap: isize,
        }

        impl AdapterAddresses {
            fn new() -> io::Result<Self> {
                // SAFETY: Gets the process's default heap and is safe to be called at any time.
                let heap = unsafe { GetProcessHeap() };

                // SAFETY: GetAdaptersAddresses() requires allocated memory to be passed in.
                // In the beginning 16kB are allocated via HeapAlloc() from the default process's
                // heap (see above), then passed to GetAdaptersAddresses().
                //
                // If this returns ERROR_NOT_ENOUGH_MEMORY then this was not enough memory and the
                // required amount is returned as out parameter. In that case we loop up to 10
                // times, reallocate memory via HeapReAlloc() and try again.
                //
                // On other errors the memory is freed before returning via HeapFree(), or when 10
                // iterations were reached.
                unsafe {
                    let mut alloc_len = 16_384;
                    let mut tries = 0;
                    let mut addresses: *mut IP_ADAPTER_ADDRESSES_LH = ptr::null_mut();

                    loop {
                        if tries > 10 {
                            HeapFree(heap, 0, addresses as *mut _);
                            return Err(io::Error::from(io::ErrorKind::OutOfMemory));
                        }

                        if addresses.is_null() {
                            addresses = HeapAlloc(heap, 0, alloc_len as usize) as *mut _;
                        } else {
                            addresses =
                                HeapReAlloc(heap, 0, addresses as *mut _, alloc_len as usize)
                                    as *mut _;
                        }
                        if addresses.is_null() {
                            return Err(io::Error::from(io::ErrorKind::OutOfMemory));
                        }

                        let res = GetAdaptersAddresses(
                            AF_INET,
                            GAA_FLAG_SKIP_ANYCAST
                                | GAA_FLAG_SKIP_MULTICAST
                                | GAA_FLAG_SKIP_DNS_SERVER,
                            ptr::null_mut(),
                            addresses,
                            &mut alloc_len,
                        );

                        if res == 0 {
                            return Ok(AdapterAddresses {
                                heap,
                                addresses: ptr::NonNull::new_unchecked(addresses),
                            });
                        } else if res == ERROR_NOT_ENOUGH_MEMORY {
                            tries += 1;
                            continue;
                        } else {
                            HeapFree(heap, 0, addresses as *mut _);
                            return Err(io::Error::from_raw_os_error(res as i32));
                        }
                    }
                }
            }

            fn iter(&self) -> AdapterAddressesIter {
                AdapterAddressesIter {
                    ptr: Some(self.addresses),
                    phantom: marker::PhantomData,
                }
            }
        }

        impl Drop for AdapterAddresses {
            fn drop(&mut self) {
                // SAFETY: The pointer is a valid IP_ADAPTER_ADDRESSES_LH pointer by construction
                // and dropped only once, so freeing it here is OK. It might be NULL so check for
                // that first.
                //
                // Heap is the process's heap as set in the constructor above.
                unsafe {
                    HeapFree(self.heap, 0, self.addresses.as_ptr() as *mut _);
                }
            }
        }

        struct AdapterAddressesIter<'a> {
            ptr: Option<ptr::NonNull<IP_ADAPTER_ADDRESSES_LH>>,
            phantom: marker::PhantomData<&'a AdapterAddresses>,
        }

        impl<'a> Iterator for AdapterAddressesIter<'a> {
            type Item = &'a IP_ADAPTER_ADDRESSES_LH;

            fn next(&mut self) -> Option<Self::Item> {
                match self.ptr {
                    None => None,
                    Some(ptr) => {
                        // SAFETY: The pointer is a valid IP_ADAPTER_ADDRESSES_LH pointer by
                        // construction so creating a reference to it is OK.
                        let addr = unsafe { &*ptr.as_ptr() };
                        self.ptr = ptr::NonNull::new(addr.next);
                        Some(addr)
                    }
                }
            }
        }

        struct UnicastAddressesIter<'a> {
            ptr: Option<ptr::NonNull<IP_ADAPTER_UNICAST_ADDRESS_LH>>,
            phantom: marker::PhantomData<&'a IP_ADAPTER_ADDRESSES_LH>,
        }

        impl<'a> UnicastAddressesIter<'a> {
            fn new(addresses: &'a IP_ADAPTER_ADDRESSES_LH) -> Self {
                Self {
                    ptr: ptr::NonNull::new(addresses.firstunicastaddress),
                    phantom: marker::PhantomData,
                }
            }
        }

        impl<'a> Iterator for UnicastAddressesIter<'a> {
            type Item = &'a IP_ADAPTER_UNICAST_ADDRESS_LH;

            fn next(&mut self) -> Option<Self::Item> {
                match self.ptr {
                    None => None,
                    Some(ptr) => {
                        let addr = unsafe { &*ptr.as_ptr() };
                        self.ptr = ptr::NonNull::new(addr.next);
                        Some(addr)
                    }
                }
            }
        }

        let addresses = AdapterAddresses::new().context("Failed getting adapter addresses")?;
        let mut if_infos = Vec::<InterfaceInfo>::new();
        for address in addresses.iter() {
            // SAFETY: adaptername points to a NUL-terminated ASCII name string that is valid
            // as long as its struct is
            let adaptername = unsafe { CStr::from_ptr(address.adaptername as *const c_char) }
                .to_str()
                .unwrap();

            // Skip adapters that are receive-only, can't do multicast or don't have IPv4 support
            // as they're not usable in a PTP context.
            if address.flags & ADAPTER_FLAG_RECEIVE_ONLY != 0 {
                debug!("Interface {} is receive-only interface", adaptername);
                continue;
            }
            if address.flags & ADAPTER_FLAG_NO_MULTICAST != 0 {
                debug!("Interface {} does not support multicast", adaptername);
                continue;
            }
            if address.flags & ADAPTER_FLAG_IPV4_ENABLED == 0 {
                debug!("Interface {} has no IPv4 address", adaptername);
                continue;
            }

            // Skip adapters that are loopback or not up.
            if address.iftype == IF_TYPE_SOFTWARE_LOOPBACK {
                debug!("Interface {} is loopback interface", adaptername);
                continue;
            }
            if address.operstatus != IF_OPER_STATUS_UP {
                debug!("Interface {} is not up", adaptername);
                continue;
            }

            // SAFETY: Both fields of the union are always valid
            let index = unsafe { address.anonymous.anonymous.ifindex } as usize;
            // Skip adapters that have no valid interface index as they can't be used to join the
            // PTP multicast group reliably for this interface only.
            if index == 0 {
                debug!("Interface {} has no valid interface index", adaptername);
                continue;
            }

            // SAFETY: friendlyname is a NUL-terminated UCS2/wide string or NULL.
            let friendlyname = unsafe {
                if !address.friendlyname.is_null() {
                    let len = {
                        let mut len = 0;
                        while *address.friendlyname.add(len) != 0 {
                            len += 1;
                        }
                        len
                    };

                    let f = slice::from_raw_parts(address.friendlyname, len);
                    let f = OsString::from_wide(f);
                    Some(String::from(f.to_str().unwrap()))
                } else {
                    None
                }
            };

            let mut hw_addr = None;
            if address.physicaladdresslength == 6 {
                let mut addr = [0u8; 6];
                addr.copy_from_slice(&address.physicaladdress[..6]);
                hw_addr = Some(addr);
            }

            let ip_addr = UnicastAddressesIter::new(address).find_map(|addr| {
                if addr.address.lpsocketaddr.is_null() {
                    return None;
                }

                // SAFETY: lpsocketaddr is a valid, non-NULL socket address and its family
                // field can be read to distinguish IPv4 and other socket addresses
                if unsafe { (*addr.address.lpsocketaddr).sa_family } != AF_INET as u16 {
                    return None;
                }

                Some(Ipv4Addr::from(
                    // SAFETY: lpsocketaddr is a valid, non-NULL IPv4 socket address as checked
                    // above and can be dereferenced as such.
                    unsafe {
                        (*addr.address.lpsocketaddr)
                            .in_addr
                            .S_un
                            .S_addr
                            .to_ne_bytes()
                    },
                ))
            });

            if let Some(ip_addr) = ip_addr {
                if_infos.push(InterfaceInfo {
                    name: String::from(adaptername),
                    other_name: friendlyname,
                    index,
                    ip_addr,
                    hw_addr,
                });
            } else {
                debug!("Interface {} has no IPv4 address", adaptername);
            }
        }

        Ok(if_infos)
    }

    /// Create an `UdpSocket` and bind it to the given address but set `SO_REUSEADDR` and/or
    /// `SO_REUSEPORT` before doing so.
    ///
    /// `UdpSocket::bind()` does not allow setting custom options before binding.
    pub fn create_udp_socket(addr: &Ipv4Addr, port: u16) -> Result<UdpSocket, io::Error> {
        use std::os::windows::io::FromRawSocket;

        // XXX: Make sure Rust std is calling WSAStartup()
        let _ = UdpSocket::bind(SocketAddr::from((Ipv4Addr::UNSPECIFIED, 0)))?;

        /// Helper struct to keep a raw socket and close it on drop
        struct Socket(SOCKET);
        impl Drop for Socket {
            fn drop(&mut self) {
                unsafe {
                    // SAFETY: The socket is valid by construction.
                    let _ = closesocket(self.0);
                }
            }
        }

        // SAFETY: Calling WSASocketW() is safe at any time and will simply fail if invalid parameters
        // are passed or something else goes wrong.
        let socket = unsafe {
            let res = WSASocketW(
                AF_INET as _,
                SOCK_DGRAM as _,
                0,
                ptr::null_mut(),
                0,
                WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT,
            );
            if res == INVALID_SOCKET {
                return Err(io::Error::from_raw_os_error(WSAGetLastError()));
            }
            Socket(res)
        };

        // SAFETY: A valid socket is passed here.
        unsafe {
            set_reuse(socket.0);
        }

        // SAFETY: A valid socket fd is passed together with a valid SOCKADDR and its size.
        unsafe {
            let addr = SOCKADDR {
                sa_family: AF_INET as _,
                sin_port: u16::to_be(port),
                in_addr: IN_ADDR {
                    S_un: IN_ADDR_0 {
                        S_addr: u32::from_ne_bytes(addr.octets()),
                    },
                },
                sin_zero: [0; 8],
            };
            let res = bind(socket.0, &addr, mem::size_of_val(&addr) as _);
            if res < 0 {
                return Err(io::Error::from_raw_os_error(WSAGetLastError()));
            }
        }

        unsafe { Ok(UdpSocket::from_raw_socket(mem::ManuallyDrop::new(socket).0)) }
    }

    // Join multicast address for a given interface.
    pub fn join_multicast_v4(
        socket: &UdpSocket,
        addr: &Ipv4Addr,
        iface: &InterfaceInfo,
    ) -> Result<(), Error> {
        let mreq = IP_MREQ {
            imr_multiaddr: IN_ADDR {
                S_un: IN_ADDR_0 {
                    S_addr: u32::from_ne_bytes(addr.octets()),
                },
            },
            imr_address: IN_ADDR {
                S_un: IN_ADDR_0 {
                    S_addr: u32::from_ne_bytes(Ipv4Addr::new(0, 0, 0, iface.index as u8).octets()),
                },
            },
        };

        // SAFETY: Requires a valid ip_mreq struct to be passed together with its size for checking
        // validity. On errors a negative integer is returned.
        unsafe {
            if setsockopt(
                socket.as_raw_socket(),
                IPPROTO_IP as i32,
                IP_ADD_MEMBERSHIP as i32,
                &mreq as *const _ as *const _,
                mem::size_of_val(&mreq) as _,
            ) < 0
            {
                bail!(
                    source: io::Error::from_raw_os_error(WSAGetLastError()),
                    "Failed joining multicast group for interface {}",
                    iface.name,
                );
            }
        }

        Ok(())
    }

    /// Allow multiple sockets to bind to the same address / port.
    ///
    /// This is best-effort and might not actually do anything.
    ///
    /// SAFETY: Must be called with a valid socket.
    unsafe fn set_reuse(socket: SOCKET) {
        // SAFETY: SO_REUSEADDR takes an i32 value that can be 0/false or 1/true and
        // enables the given feature on the socket.
        //
        // We explicitly ignore errors here. If it works, good, if it doesn't then not much
        // lost other than the ability to run multiple processes at once.
        unsafe {
            let v = 1i32;
            let res = setsockopt(
                socket,
                SOL_SOCKET as i32,
                SO_REUSEADDR as i32,
                &v as *const _ as *const _,
                mem::size_of_val(&v) as _,
            );
            if res < 0 {
                warn!("Failed to set SO_REUSEADDR on socket");
            }
        }
    }
}

pub use imp::*;

#[cfg(test)]
mod test {
    #[test]
    fn test_query_interfaces() {
        let ifaces = super::query_interfaces().unwrap();
        for iface in ifaces {
            assert!(!iface.name.is_empty());
            assert_ne!(iface.index, 0);
            assert!(!iface.ip_addr.is_unspecified());
        }
    }

    #[test]
    fn test_create_socket_join_multicast() {
        let ifaces = super::query_interfaces().unwrap();
        let iface = if ifaces.is_empty() {
            return;
        } else {
            &ifaces[0]
        };

        let socket = super::create_udp_socket(&std::net::Ipv4Addr::UNSPECIFIED, 0).unwrap();
        super::join_multicast_v4(&socket, &std::net::Ipv4Addr::new(224, 0, 0, 1), iface).unwrap();

        let local_addr = socket.local_addr().unwrap();

        let socket2 = std::net::UdpSocket::bind(std::net::SocketAddr::from((
            std::net::Ipv4Addr::UNSPECIFIED,
            0,
        )))
        .unwrap();
        socket2
            .send_to(
                &[1, 2, 3, 4],
                std::net::SocketAddr::from((std::net::Ipv4Addr::LOCALHOST, local_addr.port())),
            )
            .unwrap();
        let mut buf = [0u8; 4];
        socket.recv(&mut buf).unwrap();
        assert_eq!(buf, [1, 2, 3, 4]);
    }
}
