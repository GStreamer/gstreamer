// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use std::net::UdpSocket;

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum SocketType {
    EventSocket,
    GeneralSocket,
}

/// Result of polling the inputs of the `Poll`.
///
/// Note that reading from the sockets is non-blocking but reading from stdin is blocking so
/// special care has to be taken to only read as much as is available.
pub struct PollResult<'a> {
    ready_sockets: &'a [(usize, SocketType, &'a UdpSocket)],
    sockets: &'a [(UdpSocket, UdpSocket)],
    stdin: Option<&'a Stdin>,
    stdout: &'a Stdout,
}

impl<'a> PollResult<'a> {
    /// Returns the sockets that are currently ready for reading.
    pub fn ready_sockets(&self) -> &[(usize, SocketType, &UdpSocket)] {
        self.ready_sockets
    }

    /// Returns the event socket.
    pub fn event_socket(&self, idx: usize) -> &UdpSocket {
        &self.sockets[idx].0
    }

    /// Returns the general socket.
    pub fn general_socket(&self, idx: usize) -> &UdpSocket {
        &self.sockets[idx].1
    }

    /// Returns standard input if there is data to read.
    pub fn stdin(&self) -> Option<&Stdin> {
        self.stdin
    }

    /// Returns standard output.
    pub fn stdout(&self) -> &Stdout {
        self.stdout
    }
}

#[cfg(unix)]
mod imp {
    use super::{PollResult, SocketType};

    use std::{
        io::{self, Read, Write},
        mem,
        net::UdpSocket,
        os::unix::io::{AsRawFd, RawFd},
    };

    use crate::{bail, error::Error, ffi::unix::*};

    /// Inputs and outputs, and allowing to poll the inputs for available data.
    ///
    /// This carries the event/general UDP socket and stdin/stdout.
    pub struct Poll {
        sockets: Vec<(UdpSocket, UdpSocket)>,
        stdin: Stdin,
        stdout: Stdout,
        pollfd: Vec<pollfd>,
        results_cache: Vec<(usize, SocketType, &'static UdpSocket)>,
    }

    #[cfg(test)]
    /// A file descriptor pair representing a pipe for testing purposes.
    pub struct Pipe {
        pub read: i32,
        pub write: i32,
    }

    #[cfg(test)]
    impl Pipe {
        fn new() -> io::Result<Self> {
            // SAFETY: Requires two integers to be passed in and creates the read
            // and write end of a pipe.
            unsafe {
                let mut fds = std::mem::MaybeUninit::<[i32; 2]>::uninit();
                let res = pipe(fds.as_mut_ptr() as *mut i32);
                if res == 0 {
                    let fds = fds.assume_init();
                    Ok(Pipe {
                        read: fds[0],
                        write: fds[1],
                    })
                } else {
                    Err(io::Error::last_os_error())
                }
            }
        }
    }

    #[cfg(test)]
    impl Drop for Pipe {
        fn drop(&mut self) {
            // SAFETY: Only ever created with valid fds
            unsafe {
                close(self.read);
                close(self.write);
            }
        }
    }

    #[cfg(test)]
    impl Read for &Pipe {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            // SAFETY: read() requires a valid fd and a mutable buffer with the given size.
            // The fd is valid by construction as is the buffer.
            //
            // read() will return the number of bytes read or a negative value on errors.
            let res = unsafe { read(self.read, buf.as_mut_ptr(), buf.len()) };

            if res < 0 {
                Err(std::io::Error::last_os_error())
            } else {
                Ok(res as usize)
            }
        }
    }

    #[cfg(test)]
    impl Write for Pipe {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            // SAFETY: write() requires a valid fd and a mutable buffer with the given size.
            // The fd is valid by construction as is the buffer.
            //
            // write() will return the number of bytes written or a negative value on errors.
            let res = unsafe { write(self.write, buf.as_ptr(), buf.len()) };

            if res == -1 {
                Err(std::io::Error::last_os_error())
            } else {
                Ok(res as usize)
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    impl Poll {
        /// Name of the input based on the `struct pollfd` index.
        fn fd_name(idx: usize, len: usize) -> &'static str {
            match idx {
                i if i == len - 1 => "stdin",
                i if i % 2 == 0 => "event socket",
                i if i % 2 == 1 => "general socket",
                _ => unreachable!(),
            }
        }

        /// Create a new `Poll` instance from the pairs of sockets.
        pub fn new(sockets: Vec<(UdpSocket, UdpSocket)>) -> Result<Self, Error> {
            let stdin = Stdin::acquire();
            let stdout = Stdout::acquire();
            let n_sockets = sockets.len();

            Ok(Self {
                sockets,
                stdin,
                stdout,
                pollfd: Vec::with_capacity(n_sockets * 2 + 1),
                results_cache: Vec::with_capacity(n_sockets * 2),
            })
        }

        #[cfg(test)]
        /// Create a new `Poll` instance for testing purposes.
        ///
        /// The returned `Pipe`s are for stdin and stdout.
        pub fn new_test(
            event_socket: UdpSocket,
            general_socket: UdpSocket,
        ) -> Result<(Self, Pipe, Pipe), Error> {
            let stdin = Pipe::new().unwrap();
            let stdout = Pipe::new().unwrap();

            Ok((
                Self {
                    sockets: vec![(event_socket, general_socket)],
                    stdin: Stdin(stdin.read),
                    stdout: Stdout(stdout.write),
                    pollfd: Vec::with_capacity(3),
                    results_cache: Vec::with_capacity(2),
                },
                stdin,
                stdout,
            ))
        }

        /// Reference to the event socket.
        #[allow(unused)]
        pub fn event_socket(&self, iface: usize) -> &UdpSocket {
            &self.sockets[iface].0
        }

        /// Reference to the general socket.
        #[allow(unused)]
        pub fn general_socket(&self, iface: usize) -> &UdpSocket {
            &self.sockets[iface].1
        }

        /// Reference to stdin for reading.
        #[allow(unused)]
        pub fn stdin(&self) -> &Stdin {
            &self.stdin
        }

        /// Reference to stdout for writing.
        pub fn stdout(&self) -> &Stdout {
            &self.stdout
        }

        /// Poll the event socket, general socket and stdin for available data to read.
        ///
        /// This blocks until at least one input has data available.
        pub fn poll<'a>(&'a mut self) -> Result<PollResult<'a>, Error> {
            self.pollfd.clear();
            for (event_socket, general_socket) in &self.sockets {
                self.pollfd.push(pollfd {
                    fd: event_socket.as_raw_fd(),
                    events: POLLIN,
                    revents: 0,
                });
                self.pollfd.push(pollfd {
                    fd: general_socket.as_raw_fd(),
                    events: POLLIN,
                    revents: 0,
                });
            }

            self.pollfd.push(pollfd {
                fd: self.stdin.0,
                events: POLLIN,
                revents: 0,
            });

            // SAFETY: Polls the given pollfds above and requires a valid number to be passed.
            // A negative timeout means that it will wait until at least one of the pollfds is
            // ready.
            //
            // Will return -1 on error, otherwise the number of ready pollfds. This can never be
            // zero as a non-empty set of pollfds is passed.
            //
            // On EINTR polling should be retried.
            unsafe {
                loop {
                    let res = poll(self.pollfd[..].as_mut_ptr(), self.pollfd.len() as _, -1);
                    if res == -1 {
                        let err = std::io::Error::last_os_error();
                        if err.kind() == std::io::ErrorKind::Interrupted {
                            continue;
                        }
                        bail!(source: err, "Failed polling");
                    }
                    assert_ne!(res, 0);
                    break;
                }
            }

            // Check for errors or hangup first
            for (idx, pfd) in self.pollfd.iter().enumerate() {
                if pfd.revents & (POLLERR | POLLNVAL) != 0 {
                    bail!(
                        "Poll error on {} for interface {}",
                        Self::fd_name(idx, self.pollfd.len()),
                        idx / 2
                    );
                }

                if pfd.revents & POLLHUP != 0 {
                    bail!(
                        "Hang up during polling on {} for interface {}",
                        Self::fd_name(idx, self.pollfd.len()),
                        idx / 2
                    );
                }
            }

            self.results_cache.clear();
            // SAFETY: References have the same memory representation independent of lifetime
            let ready_sockets = unsafe {
                mem::transmute::<
                    &mut Vec<(usize, SocketType, &'static UdpSocket)>,
                    &mut Vec<(usize, SocketType, &'a UdpSocket)>,
                >(&mut self.results_cache)
            };

            for (idx, pfd) in self.pollfd.iter().enumerate() {
                if pfd.revents & POLLIN != 0 {
                    if idx == self.pollfd.len() - 1 {
                        break;
                    }
                    if idx % 2 == 0 {
                        ready_sockets.push((
                            idx / 2,
                            SocketType::EventSocket,
                            &self.sockets[idx / 2].0,
                        ));
                    } else {
                        ready_sockets.push((
                            idx / 2,
                            SocketType::GeneralSocket,
                            &self.sockets[idx / 2].1,
                        ));
                    }
                }
            }

            Ok(PollResult {
                ready_sockets: &*ready_sockets,
                sockets: &self.sockets,
                stdin: if self.pollfd[self.pollfd.len() - 1].revents & POLLIN != 0 {
                    Some(&self.stdin)
                } else {
                    None
                },
                stdout: &self.stdout,
            })
        }
    }

    /// Raw, unbuffered handle to `stdin`.
    ///
    /// This implements the `Read` trait for reading.
    pub struct Stdin(RawFd);

    impl Stdin {
        fn acquire() -> Self {
            Stdin(STDIN_FILENO)
        }
    }

    impl Read for Stdin {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            <&Stdin as Read>::read(&mut &*self, buf)
        }
    }

    impl Read for &Stdin {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
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

    /// Raw, unbuffered handle to `stdout`.
    ///
    /// This implements the `Write` trait for writing.
    pub struct Stdout(RawFd);

    impl Stdout {
        fn acquire() -> Self {
            Stdout(STDOUT_FILENO)
        }
    }

    impl Write for Stdout {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            <&Stdout as Write>::write(&mut &*self, buf)
        }

        fn flush(&mut self) -> io::Result<()> {
            <&Stdout as Write>::flush(&mut &*self)
        }
    }

    impl Write for &Stdout {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            // SAFETY: write() requires a valid fd and a mutable buffer with the given size.
            // The fd is valid by construction as is the buffer.
            //
            // write() will return the number of bytes written or a negative value on errors.
            let res = unsafe { write(self.0, buf.as_ptr(), buf.len()) };

            if res == -1 {
                Err(std::io::Error::last_os_error())
            } else {
                Ok(res as usize)
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    /// Raw, unbuffered handle to `stderr`.
    ///
    /// This implements the `Write` trait for writing and is implemented as a singleton to allow
    /// usage from everywhere at any time for logging purposes.
    ///
    /// This does not implement any locking so usage from multiple threads at once will likely
    /// cause interleaved output.
    pub struct Stderr(RawFd);

    impl Stderr {
        #[cfg(not(test))]
        pub fn acquire() -> Self {
            Stderr(STDERR_FILENO)
        }
    }

    impl Write for Stderr {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            <&Stderr as Write>::write(&mut &*self, buf)
        }

        fn flush(&mut self) -> io::Result<()> {
            <&Stderr as Write>::flush(&mut &*self)
        }
    }

    impl Write for &Stderr {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            // SAFETY: write() requires a valid fd and a mutable buffer with the given size.
            // The fd is valid by construction as is the buffer.
            //
            // write() will return the number of bytes written or a negative value on errors.
            let res = unsafe { write(self.0, buf.as_ptr(), buf.len()) };

            if res == -1 {
                Err(std::io::Error::last_os_error())
            } else {
                Ok(res as usize)
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
}

#[cfg(windows)]
mod imp {
    use super::{PollResult, SocketType};

    use std::{
        cmp,
        io::{self, Read, Write},
        mem,
        net::UdpSocket,
        os::windows::{io::AsRawSocket, raw::HANDLE},
        ptr,
        sync::{Arc, Condvar, Mutex},
        thread,
    };

    use crate::{
        bail,
        error::{Context, Error},
        ffi::windows::*,
    };

    /// Inputs and outputs, and allowing to poll the inputs for available data.
    ///
    /// This carries the event/general UDP socket and stdin/stdout.
    pub struct Poll {
        sockets: Vec<(UdpSocket, UdpSocket)>,
        events: Vec<(EventHandle, EventHandle)>,
        stdin: Stdin,
        stdout: Stdout,
        handles: Vec<HANDLE>,
        results_cache: Vec<(usize, SocketType, &'static UdpSocket)>,
    }

    /// Helper struct for a WSA event.
    struct EventHandle(HANDLE);

    impl EventHandle {
        fn new() -> io::Result<Self> {
            // SAFETY: WSACreateEvent() returns 0 on error or otherwise a valid WSA event
            // that has to be closed again later.
            unsafe {
                let event = WSACreateEvent();
                if event.is_null() || event == INVALID_HANDLE_VALUE {
                    Err(io::Error::from_raw_os_error(WSAGetLastError()))
                } else {
                    Ok(EventHandle(event))
                }
            }
        }
    }

    impl Drop for EventHandle {
        fn drop(&mut self) {
            // SAFETY: The event is valid by construction and dropped at most once, so can be
            // safely closed here..
            //
            // The return value is intentionally ignored as nothing else can be done
            // on errors anyway.
            unsafe {
                let _ = WSACloseEvent(self.0);
            }
        }
    }

    #[cfg(test)]
    pub struct Pipe {
        read: HANDLE,
        write: HANDLE,
    }

    #[cfg(test)]
    impl Drop for Pipe {
        fn drop(&mut self) {
            // SAFETY: Both handles are by construction valid up there.
            unsafe {
                CloseHandle(self.read);
                CloseHandle(self.write);
            }
        }
    }

    #[cfg(test)]
    impl Pipe {
        fn new() -> io::Result<Self> {
            // SAFETY: On success returns a non-zero integer and stores read/write handles in the
            // two out pointers, which will have to be closed again later.
            unsafe {
                let mut readpipe = mem::MaybeUninit::uninit();
                let mut writepipe = mem::MaybeUninit::uninit();

                let res = CreatePipe(
                    readpipe.as_mut_ptr(),
                    writepipe.as_mut_ptr(),
                    ptr::null_mut(),
                    0,
                );

                if res != 0 {
                    Ok(Self {
                        read: readpipe.assume_init(),
                        write: writepipe.assume_init(),
                    })
                } else {
                    Err(io::Error::last_os_error())
                }
            }
        }
    }

    #[cfg(test)]
    impl Read for &Pipe {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            // SAFETY: Reads the given number of bytes into the buffer from the stdin handle.
            unsafe {
                let mut lpnumberofbytesread = mem::MaybeUninit::uninit();
                let res = ReadFile(
                    self.read,
                    buf.as_mut_ptr(),
                    cmp::min(buf.len() as u32, u32::MAX) as u32,
                    lpnumberofbytesread.as_mut_ptr(),
                    ptr::null_mut(),
                );

                if res == 0 {
                    Err(io::Error::last_os_error())
                } else {
                    Ok(lpnumberofbytesread.assume_init() as usize)
                }
            }
        }
    }

    #[cfg(test)]
    impl Write for Pipe {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            // SAFETY: Writes the given number of bytes to stdout or at most u32::MAX. On error
            // zero is returned, otherwise the number of bytes written is set accordingly and
            // returned.
            unsafe {
                let mut lpnumberofbyteswritten = mem::MaybeUninit::uninit();
                let res = WriteFile(
                    self.write,
                    buf.as_ptr(),
                    cmp::min(buf.len() as u32, u32::MAX) as u32,
                    lpnumberofbyteswritten.as_mut_ptr(),
                    ptr::null_mut(),
                );

                if res == 0 {
                    Err(io::Error::last_os_error())
                } else {
                    Ok(lpnumberofbyteswritten.assume_init() as usize)
                }
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    impl Poll {
        /// Internal constructor.
        pub fn new_internal(
            sockets: Vec<(UdpSocket, UdpSocket)>,
            stdin: Stdin,
            stdout: Stdout,
        ) -> Result<Self, Error> {
            // Create event objects for the readability of the sockets.
            let events = sockets
                .iter()
                .map(|(_event_socket, _general_socket)| -> Result<_, Error> {
                    Ok((
                        EventHandle::new().context("Failed creating WSA event")?,
                        EventHandle::new().context("Failed creating WSA event")?,
                    ))
                })
                .collect::<Result<Vec<_>, Error>>()?;

            for ((event_socket, general_socket), (event_socket_event, general_socket_event)) in
                Iterator::zip(sockets.iter(), events.iter())
            {
                // SAFETY: WSAEventSelect() requires a valid socket and WSA event, which are both
                // passed here, and the bitflag of events that should be selected for.
                //
                // On error a non-zero value is returned.
                unsafe {
                    if WSAEventSelect(event_socket.as_raw_socket(), event_socket_event.0, FD_READ)
                        != 0
                    {
                        bail!(
                            source: io::Error::from_raw_os_error(WSAGetLastError()),
                            "Failed selecting for read events on event socket"
                        );
                    }

                    if WSAEventSelect(
                        general_socket.as_raw_socket(),
                        general_socket_event.0,
                        FD_READ,
                    ) != 0
                    {
                        bail!(
                            source: io::Error::from_raw_os_error(WSAGetLastError()),
                            "Failed selecting for read events on general socket"
                        );
                    }
                }
            }

            let n_sockets = sockets.len();

            Ok(Self {
                sockets,
                events,
                stdin,
                stdout,
                handles: Vec::with_capacity(n_sockets * 2 + 1),
                results_cache: Vec::with_capacity(1),
            })
        }

        /// Create a new `Poll` instance from the two sockets.
        pub fn new(sockets: Vec<(UdpSocket, UdpSocket)>) -> Result<Self, Error> {
            let stdin = Stdin::acquire().context("Failure acquiring stdin handle")?;
            let stdout = Stdout::acquire().context("Failed acquiring stdout handle")?;

            Self::new_internal(sockets, stdin, stdout)
        }

        #[cfg(test)]
        /// Create a new `Poll` instance for testing purposes.
        ///
        /// The returned `Pipe`s are for stdin and stdout.
        pub fn new_test(
            event_socket: UdpSocket,
            general_socket: UdpSocket,
        ) -> Result<(Self, Pipe, Pipe), Error> {
            let stdin_pipe = Pipe::new().unwrap();
            let stdout_pipe = Pipe::new().unwrap();

            let stdin =
                Stdin::from_handle(stdin_pipe.read).context("Failure acquiring stdin handle")?;
            let stdout =
                Stdout::from_handle(stdout_pipe.write).context("Failed acquiring stdout handle")?;

            let poll = Self::new_internal(vec![(event_socket, general_socket)], stdin, stdout)?;

            Ok((poll, stdin_pipe, stdout_pipe))
        }

        /// Reference to the event socket.
        #[allow(unused)]
        pub fn event_socket(&self, iface: usize) -> &UdpSocket {
            &self.sockets[iface].0
        }

        /// Reference to the general socket.
        #[allow(unused)]
        pub fn general_socket(&self, iface: usize) -> &UdpSocket {
            &self.sockets[iface].1
        }

        /// Reference to stdin for reading.
        #[allow(unused)]
        pub fn stdin(&self) -> &Stdin {
            &self.stdin
        }

        /// Reference to stdout for writing.
        pub fn stdout(&self) -> &Stdout {
            &self.stdout
        }

        /// Poll the event socket, general socket and stdin for available data to read.
        ///
        /// This blocks until at least one input has data available.
        pub fn poll<'a>(&'a mut self) -> Result<PollResult<'a>, Error> {
            self.handles.clear();

            for (event_socket_event, general_socket_event) in &self.events {
                self.handles.push(event_socket_event.0);
                self.handles.push(general_socket_event.0);
            }

            self.handles.push(
                // If stdin is a pipe then we use the signalling event, otherwise stdin itself.
                if let Some(ref thread_state) = self.stdin.thread_state {
                    thread_state.event
                } else {
                    self.stdin.handle
                },
            );

            // If stdin is a pipe and currently no data is pending on it then signal
            // the reading thread to try reading one byte and blocking for that long.
            if let Some(ref mut thread_state) = self.stdin.thread_state {
                let mut guard = thread_state.buffer.lock().unwrap();
                if !guard.buffer_filled && !guard.fill_buffer {
                    guard.fill_buffer = true;
                    // SAFETY: The thread's event is valid by construction until the thread
                    // is stopped, and can be reset at any time.
                    unsafe {
                        ResetEvent(thread_state.event);
                    }
                    thread_state.buffer_cond.notify_one();
                }
            }

            // SAFETY: Wait for the socket/stdin objects to become ready. This requires a valid
            // array of valid handles and the corresponding length, whether it should wait for all
            // handles (no), and a timeout (infinity).
            //
            // On error u32::MAX is returned, otherwise an index into the array of handles is
            // returned for the handle that became ready.
            let res = unsafe {
                let res = WaitForMultipleObjects(
                    self.handles.len() as _,
                    self.handles[..].as_ptr(),
                    0,
                    u32::MAX,
                );
                if res == u32::MAX {
                    bail!(
                        source: io::Error::from_raw_os_error(WSAGetLastError()),
                        "Failed waiting for events"
                    );
                }

                assert!(
                    (0..self.handles.len()).contains(&(res as usize)),
                    "Unexpected WaitForMultipleObjects() return value {}",
                    res,
                );

                res as usize
            };

            self.results_cache.clear();
            // SAFETY: References have the same memory representation independent of lifetime
            let ready_sockets = unsafe {
                mem::transmute::<
                    &mut Vec<(usize, SocketType, &'static UdpSocket)>,
                    &mut Vec<(usize, SocketType, &'a UdpSocket)>,
                >(&mut self.results_cache)
            };

            // For the sockets, enumerate the events that woke up the waiting, collect any errors
            // and reset the event objects.
            if res < self.handles.len() - 1 {
                let (socket, event) = if res % 2 == 0 {
                    (&self.sockets[res / 2].0, &self.events[res / 2].0)
                } else {
                    (&self.sockets[res / 2].1, &self.events[res / 2].1)
                };

                // SAFETY: Requires a valid socket and event, which is given by construction here.
                // The passed in memory for the network events will be filled if no error happens,
                // and the function returns a non-zero value if an error has happened.
                let networkevents = unsafe {
                    let mut networkevents = mem::MaybeUninit::uninit();
                    if WSAEnumNetworkEvents(
                        socket.as_raw_socket(),
                        event.0,
                        networkevents.as_mut_ptr(),
                    ) != 0
                    {
                        bail!(
                            source: io::Error::from_raw_os_error(WSAGetLastError()),
                            "Failed enumerating network events on {} socket for interface {}",
                            if res % 2 == 0 { "event" } else { "general" },
                            res / 2,
                        );
                    }

                    networkevents.assume_init()
                };

                if networkevents.ierrorcode[FD_READ_BIT] != 0 {
                    bail!(
                        source: io::Error::from_raw_os_error(networkevents.ierrorcode[FD_READ_BIT]),
                        "Error on {} socket for interface {} while waiting for events",
                        if res == 0 { "event" } else { "general" },
                        res / 2,
                    );
                }

                // FIXME: This seems to happen every now and then although it shouldn't, and in
                // that case a packet always seems to be queued up on the socket. As the sockets
                // are non-blocking it also wouldn't be a problem otherwise, so let's just log this
                // here and ignore it.
                if networkevents.lnetworkevents & FD_READ == 0 {
                    debug!(
                        "Socket {} woke up but has neither an error nor a FD_READ event",
                        res
                    );
                }
                ready_sockets.push((
                    res / 2,
                    if res % 2 == 0 {
                        SocketType::EventSocket
                    } else {
                        SocketType::GeneralSocket
                    },
                    socket,
                ));
            }

            Ok(PollResult {
                ready_sockets: &*ready_sockets,
                sockets: &self.sockets,
                stdin: if res == self.handles.len() - 1 {
                    Some(&self.stdin)
                } else {
                    None
                },
                stdout: &self.stdout,
            })
        }
    }

    /// Raw, unbuffered handle to `stdin`.
    ///
    /// This implements the `Read` trait for reading.
    pub struct Stdin {
        handle: HANDLE,
        thread_state: Option<Arc<StdinThreadState>>,
        join_handle: Option<thread::JoinHandle<()>>,
    }

    struct StdinThreadState {
        buffer: Mutex<StdinBuffer>,
        buffer_cond: Condvar,
        event: HANDLE,
        handle: HANDLE,
    }

    unsafe impl Send for StdinThreadState {}
    unsafe impl Sync for StdinThreadState {}

    struct StdinBuffer {
        buffer: [u8; 1],
        error: Option<io::Error>,
        buffer_filled: bool,
        fill_buffer: bool,
        shutdown: bool,
    }

    impl Drop for Stdin {
        fn drop(&mut self) {
            // If stdin was a pipe and a thread was started to check for read-readiness
            // then stop this thread now and release its resources.
            if let Some(ref thread_state) = self.thread_state {
                let mut guard = thread_state.buffer.lock().unwrap();
                guard.shutdown = true;
                thread_state.buffer_cond.notify_one();
                drop(guard);
                let _ = self.join_handle.take().unwrap().join();

                // SAFETY: The thread is stopped now so the event is not used by anything else
                // anymore and can safely be closed now.
                //
                // The return value is explicitly ignored because nothing can be done on error
                // anyway.
                unsafe {
                    let _ = CloseHandle(thread_state.event);
                }
            }
        }
    }

    impl Stdin {
        fn acquire() -> Result<Self, Error> {
            // SAFETY: GetStdHandle returns a borrowed handle, or 0 if none is set or -1 if an
            // error has happened.
            let handle = unsafe {
                let handle = GetStdHandle(STD_INPUT_HANDLE);
                if handle.is_null() {
                    bail!("No stdin handle set");
                } else if handle == INVALID_HANDLE_VALUE {
                    bail!(source: io::Error::last_os_error(), "Can't get stdin handle");
                }

                handle
            };

            Self::from_handle(handle)
        }

        fn from_handle(handle: HANDLE) -> Result<Self, Error> {
            // SAFETY: GetFileType() is safe to call on any valid handle.
            let type_ = unsafe { GetFileType(handle) };

            if type_ == FILE_TYPE_CHAR {
                // Set the console to raw mode and flush any pending input.
                //
                // SAFETY: Calling this on non-console handles will cause an error but otherwise
                // have no negative effects. We can safely change the console mode here as nothing
                // else is accessing the console.
                unsafe {
                    let _ = SetConsoleMode(handle, 0);
                    let _ = FlushConsoleInputBuffer(handle);
                }

                Ok(Stdin {
                    handle,
                    thread_state: None,
                    join_handle: None,
                })
            } else if type_ == FILE_TYPE_PIPE {
                // XXX: Because g_spawn() creates the overridden pipes with _pipe(), they're
                //        1. Full duplex, so WaitForMultipleObjects() always considers them ready as you can write
                //        2. Not overlapped so only synchronous IO can be used
                //      To work around this we're creating a thread here that just reads synchronously
                //      from stdin to signal ready-ness.

                // SAFETY: Creating an event handle with all-zero parameters is valid and on error
                // a NULL handle will be returned. Otherwise a valid event handle is returned that
                // needs to be closed again later, which happens as part of the StdinThreadState
                // Drop implementation.
                let event = unsafe {
                    let event = CreateEventA(ptr::null(), 0, 0, ptr::null());
                    if event.is_null() {
                        bail!(
                            source: io::Error::last_os_error(),
                            "Failed creating event handle"
                        );
                    }

                    event
                };
                let thread_state = Arc::new(StdinThreadState {
                    buffer: Mutex::new(StdinBuffer {
                        buffer: [0],
                        error: None,
                        buffer_filled: false,
                        fill_buffer: true,
                        shutdown: false,
                    }),
                    buffer_cond: Condvar::new(),
                    event,
                    handle,
                });

                let join_handle = thread::spawn({
                    let thread_state = thread_state.clone();
                    move || Self::stdin_readiness_thread(&thread_state)
                });

                Ok(Stdin {
                    handle,
                    thread_state: Some(thread_state),
                    join_handle: Some(join_handle),
                })
            } else {
                bail!("unhandled stdin handle type {:x}", type_);
            }
        }

        /// Thread function to signal readiness of stdin.
        ///
        /// This thread tries to read a single byte and buffers it, then signals an event
        /// object and waits for reading to finish and a new call to `poll()` to
        /// start trying to read a single byte again.
        fn stdin_readiness_thread(thread_state: &StdinThreadState) {
            loop {
                let mut buffer = [0u8];
                // SAFETY: Reads one byte from the handle synchronously. Nothing else is currently
                // reading from the handle as this thread is waiting below on the condition
                // variable as long as a single byte was read already, and only wakes up again if a
                // full packet was read from stdin and the other thread is waiting on the event
                // handle again.
                let res = unsafe {
                    let mut bytes_read = mem::MaybeUninit::uninit();
                    let res = ReadFile(
                        thread_state.handle,
                        buffer[..].as_mut_ptr(),
                        buffer.len() as u32,
                        bytes_read.as_mut_ptr(),
                        ptr::null_mut(),
                    );
                    if res == 0 {
                        Err(io::Error::last_os_error())
                    } else {
                        Ok(bytes_read.assume_init())
                    }
                };

                let mut guard = thread_state.buffer.lock().unwrap();
                assert!(!guard.buffer_filled);
                assert!(guard.fill_buffer);
                if guard.shutdown {
                    break;
                }
                guard.buffer_filled = true;
                guard.fill_buffer = false;
                match res {
                    Err(err) => {
                        guard.error = Some(err);
                    }
                    Ok(bytes_read) => {
                        guard.buffer[0] = buffer[0];
                        assert_eq!(bytes_read, 1);
                    }
                }

                // SAFETY: Signalling an event is valid from any thread at any time and the event
                // handle is valid by construction.
                unsafe {
                    SetEvent(thread_state.event);
                }
                while !guard.shutdown && !guard.fill_buffer {
                    guard = thread_state.buffer_cond.wait(guard).unwrap();
                }
                if guard.shutdown {
                    break;
                }
            }
        }
    }

    impl Read for Stdin {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            <&Stdin as Read>::read(&mut &*self, buf)
        }
    }

    impl Read for &Stdin {
        fn read(&mut self, mut buf: &mut [u8]) -> io::Result<usize> {
            if buf.is_empty() {
                return Ok(0);
            }

            // If a read byte is pending from the readiness signalling thread then
            // read that first here before reading any remaining data.
            let mut already_read = 0;
            if let Some(ref thread_state) = self.thread_state {
                let mut guard = thread_state.buffer.lock().unwrap();
                assert!(!guard.fill_buffer);
                if guard.buffer_filled {
                    guard.buffer_filled = false;
                    if let Some(err) = guard.error.take() {
                        return Err(err);
                    }
                    buf[0] = guard.buffer[0];
                    if buf.len() == 1 {
                        return Ok(1);
                    }
                    buf = &mut buf[1..];
                    already_read = 1;
                }
            }

            // SAFETY: Reads the given number of bytes into the buffer from the stdin handle.
            // The other thread is not currently reading as checked above, and would only be
            // triggered to read again by this thread once poll() is called.
            unsafe {
                let mut lpnumberofbytesread = mem::MaybeUninit::uninit();
                let res = ReadFile(
                    self.handle,
                    buf.as_mut_ptr(),
                    cmp::min(buf.len() as u32, u32::MAX) as u32,
                    lpnumberofbytesread.as_mut_ptr(),
                    ptr::null_mut(),
                );

                if res == 0 {
                    Err(io::Error::last_os_error())
                } else {
                    Ok(lpnumberofbytesread.assume_init() as usize + already_read)
                }
            }
        }
    }

    /// Raw, unbuffered handle to `stdout`.
    ///
    /// This implements the `Write` trait for writing.
    pub struct Stdout(HANDLE);

    impl Stdout {
        fn acquire() -> Result<Self, Error> {
            // SAFETY: GetStdHandle returns a borrowed handle, or 0 if none is set or -1 if an
            // error has happened.
            let handle = unsafe {
                let handle = GetStdHandle(STD_OUTPUT_HANDLE);
                if handle.is_null() {
                    bail!("No stdout handle set");
                } else if handle == INVALID_HANDLE_VALUE {
                    bail!(
                        source: io::Error::last_os_error(),
                        "Can't get stdout handle"
                    );
                }

                handle
            };

            Self::from_handle(handle)
        }

        fn from_handle(handle: HANDLE) -> Result<Self, Error> {
            // SAFETY: GetFileType() is safe to call on any valid handle.
            let type_ = unsafe { GetFileType(handle) };

            if type_ == FILE_TYPE_CHAR {
                // Set the console to raw mode.
                //
                // SAFETY: Calling this on non-console handles will cause an error but otherwise
                // have no negative effects. We can safely change the console mode here as nothing
                // else is accessing the console.
                unsafe {
                    let _ = SetConsoleMode(handle, 0);
                }
            } else if type_ != FILE_TYPE_PIPE {
                bail!("Unsupported stdout handle type {:x}", type_);
            }

            Ok(Stdout(handle))
        }
    }

    impl Write for Stdout {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            <&Stdout as Write>::write(&mut &*self, buf)
        }

        fn flush(&mut self) -> io::Result<()> {
            <&Stdout as Write>::flush(&mut &*self)
        }
    }

    impl Write for &Stdout {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            // SAFETY: Writes the given number of bytes to stdout or at most u32::MAX. On error
            // zero is returned, otherwise the number of bytes written is set accordingly and
            // returned.
            unsafe {
                let mut lpnumberofbyteswritten = mem::MaybeUninit::uninit();
                let res = WriteFile(
                    self.0,
                    buf.as_ptr(),
                    cmp::min(buf.len() as u32, u32::MAX) as u32,
                    lpnumberofbyteswritten.as_mut_ptr(),
                    ptr::null_mut(),
                );

                if res == 0 {
                    Err(io::Error::last_os_error())
                } else {
                    Ok(lpnumberofbyteswritten.assume_init() as usize)
                }
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    /// Raw, unbuffered handle to `stderr`.
    ///
    /// This implements the `Write` trait for writing and is implemented as a singleton to allow
    /// usage from everywhere at any time for logging purposes.
    ///
    /// This does not implement any locking so usage from multiple threads at once will likely
    /// cause interleaved output.
    pub struct Stderr(HANDLE);

    impl Stderr {
        #[cfg(not(test))]
        pub fn acquire() -> Self {
            use std::sync::Once;

            struct SyncHandle(HANDLE);
            // SAFETY: This is a single-threaded application and even otherwise writing from
            // multiple threads at once to a pipe is safe and will only cause interleaved output.
            unsafe impl Send for SyncHandle {}
            unsafe impl Sync for SyncHandle {}

            static mut STDERR: SyncHandle = SyncHandle(INVALID_HANDLE_VALUE);
            static STDERR_ONCE: Once = Once::new();

            STDERR_ONCE.call_once(|| {
                // SAFETY: GetStdHandle returns a borrowed handle, or 0 if none is set or -1 if an
                // error has happened.
                let handle = unsafe {
                    let handle = GetStdHandle(STD_ERROR_HANDLE);
                    if handle.is_null() {
                        return;
                    } else if handle == INVALID_HANDLE_VALUE {
                        return;
                    }

                    handle
                };

                // SAFETY: GetFileType() is safe to call on any valid handle.
                let type_ = unsafe { GetFileType(handle) };

                if type_ == FILE_TYPE_CHAR {
                    // Set the console to raw mode.
                    //
                    // SAFETY: Calling this on non-console handles will cause an error but otherwise
                    // have no negative effects. We can safely change the console mode here as nothing
                    // else is accessing the console.
                    unsafe {
                        let _ = SetConsoleMode(handle, 0);
                    }
                } else if type_ != FILE_TYPE_PIPE {
                    return;
                }

                // SAFETY: Only accessed in this function and multiple mutable accesses are
                // prevented by the `Once`.
                unsafe {
                    STDERR.0 = handle;
                }
            });

            // SAFETY: Only accesses immutably here and all mutable accesses are serialized above
            // by the `Once`.
            Stderr(unsafe { STDERR.0 })
        }
    }

    impl Write for Stderr {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            <&Stderr as Write>::write(&mut &*self, buf)
        }

        fn flush(&mut self) -> io::Result<()> {
            <&Stderr as Write>::flush(&mut &*self)
        }
    }

    impl Write for &Stderr {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            if self.0 == INVALID_HANDLE_VALUE {
                return Ok(buf.len());
            }

            // SAFETY: Writes the given number of bytes to stderr or at most u32::MAX. On error
            // zero is returned, otherwise the number of bytes written is set accordingly and
            // returned.
            unsafe {
                let mut lpnumberofbyteswritten = mem::MaybeUninit::uninit();
                let res = WriteFile(
                    self.0,
                    buf.as_ptr(),
                    cmp::min(buf.len() as u32, u32::MAX) as u32,
                    lpnumberofbyteswritten.as_mut_ptr(),
                    ptr::null_mut(),
                );

                if res == 0 {
                    Err(io::Error::last_os_error())
                } else {
                    Ok(lpnumberofbyteswritten.assume_init() as usize)
                }
            }
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
}

pub use self::imp::{Poll, Stderr, Stdin, Stdout};

#[cfg(test)]
mod test {
    #[test]
    fn test_poll() {
        use super::{Poll, SocketType};

        use std::io::prelude::*;

        let event_socket = std::net::UdpSocket::bind(std::net::SocketAddr::from((
            std::net::Ipv4Addr::LOCALHOST,
            0,
        )))
        .unwrap();
        let event_port = event_socket.local_addr().unwrap().port();

        let general_socket = std::net::UdpSocket::bind(std::net::SocketAddr::from((
            std::net::Ipv4Addr::LOCALHOST,
            0,
        )))
        .unwrap();
        let general_port = general_socket.local_addr().unwrap().port();

        let send_socket = std::net::UdpSocket::bind(std::net::SocketAddr::from((
            std::net::Ipv4Addr::LOCALHOST,
            0,
        )))
        .unwrap();

        let (mut poll, mut stdin, _stdout) = Poll::new_test(event_socket, general_socket).unwrap();

        let mut buf = [0u8; 4];

        for _ in 0..10 {
            send_socket
                .send_to(&[1, 2, 3, 4], (std::net::Ipv4Addr::LOCALHOST, event_port))
                .unwrap();
            let res = poll.poll().unwrap();
            assert_eq!(res.ready_sockets().len(), 1);
            assert_eq!(res.ready_sockets()[0].0, 0);
            assert_eq!(res.ready_sockets()[0].1, SocketType::EventSocket);
            assert_eq!(res.ready_sockets()[0].2.recv(&mut buf).unwrap(), 4);
            assert_eq!(buf, [1, 2, 3, 4]);

            send_socket
                .send_to(&[1, 2, 3, 4], (std::net::Ipv4Addr::LOCALHOST, general_port))
                .unwrap();
            let res = poll.poll().unwrap();

            assert_eq!(res.ready_sockets().len(), 1);
            assert_eq!(res.ready_sockets()[0].0, 0);
            assert_eq!(res.ready_sockets()[0].1, SocketType::GeneralSocket);
            assert_eq!(res.ready_sockets()[0].2.recv(&mut buf).unwrap(), 4);
            assert_eq!(buf, [1, 2, 3, 4]);

            stdin.write_all(&[1, 2, 3, 4]).unwrap();
            let res = poll.poll().unwrap();
            assert!(res.ready_sockets().is_empty());
            {
                let mut stdin = res.stdin();
                let stdin = stdin.as_mut().unwrap();
                stdin.read_exact(&mut buf).unwrap();
                assert_eq!(buf, [1, 2, 3, 4]);
            }
        }

        drop(poll);
    }
}
