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
mod imp {
    use std::{
        io::{self, Read, Write},
        net::UdpSocket,
        os::unix::io::{AsRawFd, RawFd},
    };

    use crate::{bail, error::Error, ffi::unix::*};

    /// Inputs and outputs, and allowing to poll the inputs for available data.
    ///
    /// This carries the event/general UDP socket and stdin/stdout.
    pub struct Poll {
        event_socket: UdpSocket,
        general_socket: UdpSocket,
        stdin: Stdin,
        stdout: Stdout,
    }

    /// Result of polling the inputs of the `Poll`.
    ///
    /// Any input that has data available for reading will be set to `true`, potentially multiple
    /// at once.
    ///
    /// Note that reading from the sockets is non-blocking but reading from stdin is blocking so
    /// special care has to be taken to only read as much as is available.
    pub struct PollResult {
        pub event_socket: bool,
        pub general_socket: bool,
        pub stdin: bool,
    }

    impl Poll {
        /// Name of the input based on the `struct pollfd` index.
        fn fd_name(idx: usize) -> &'static str {
            match idx {
                0 => "event socket",
                1 => "general socket",
                2 => "stdin",
                _ => unreachable!(),
            }
        }

        /// Create a new `Poll` instance from the two sockets.
        pub fn new(event_socket: UdpSocket, general_socket: UdpSocket) -> Result<Self, Error> {
            let stdin = Stdin::acquire();
            let stdout = Stdout::acquire();

            Ok(Self {
                event_socket,
                general_socket,
                stdin,
                stdout,
            })
        }

        /// Mutable reference to the event socket.
        pub fn event_socket(&mut self) -> &mut UdpSocket {
            &mut self.event_socket
        }

        /// Mutable reference to the general socket.
        pub fn general_socket(&mut self) -> &mut UdpSocket {
            &mut self.general_socket
        }

        /// Mutable reference to stdin for reading.
        pub fn stdin(&mut self) -> &mut Stdin {
            &mut self.stdin
        }

        /// Mutable reference to stdout for writing.
        pub fn stdout(&mut self) -> &mut Stdout {
            &mut self.stdout
        }

        /// Poll the event socket, general socket and stdin for available data to read.
        ///
        /// This blocks until at least one input has data available.
        pub fn poll(&mut self) -> Result<PollResult, Error> {
            let mut pollfd = [
                pollfd {
                    fd: self.event_socket.as_raw_fd(),
                    events: POLLIN,
                    revents: 0,
                },
                pollfd {
                    fd: self.general_socket.as_raw_fd(),
                    events: POLLIN,
                    revents: 0,
                },
                pollfd {
                    fd: self.stdin.0,
                    events: POLLIN,
                    revents: 0,
                },
            ];

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
                    let res = poll(pollfd[..].as_mut_ptr(), pollfd.len() as _, -1);
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
            for (idx, pfd) in pollfd.iter().enumerate() {
                if pfd.revents & (POLLERR | POLLNVAL) != 0 {
                    bail!("Poll error on {}", Self::fd_name(idx));
                }

                if pfd.revents & POLLHUP != 0 {
                    bail!("Hang up during polling on {}", Self::fd_name(idx));
                }
            }

            Ok(PollResult {
                event_socket: pollfd[0].revents & POLLIN != 0,
                general_socket: pollfd[1].revents & POLLIN != 0,
                stdin: pollfd[2].revents & POLLIN != 0,
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
        event_socket: UdpSocket,
        event_socket_event: EventHandle,
        general_socket: UdpSocket,
        general_socket_event: EventHandle,
        stdin: Stdin,
        stdout: Stdout,
    }

    /// Helper struct for a WSA event.
    struct EventHandle(HANDLE);

    impl EventHandle {
        fn new() -> io::Result<Self> {
            // SAFETY: WSACreateEvent() returns 0 on error or otherwise a valid WSA event
            // that has to be closed again later.
            unsafe {
                let event = WSACreateEvent();
                if event.is_null() || event as isize == -1 {
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

    /// Result of polling the inputs of the `Poll`.
    ///
    /// Any input that has data available for reading will be set to `true`, potentially multiple
    /// at once.
    ///
    /// Note that reading from the sockets is non-blocking but reading from stdin is blocking so
    /// special care has to be taken to only read as much as is available.
    pub struct PollResult {
        pub event_socket: bool,
        pub general_socket: bool,
        pub stdin: bool,
    }

    impl Poll {
        /// Create a new `Poll` instance from the two sockets.
        pub fn new(event_socket: UdpSocket, general_socket: UdpSocket) -> Result<Self, Error> {
            let stdin = Stdin::acquire().context("Failure acquiring stdin handle")?;
            let stdout = Stdout::acquire().context("Failed acquiring stdout handle")?;

            // Create event objects for the readability of the sockets.
            let event_socket_event = EventHandle::new().context("Failed creating WSA event")?;
            let general_socket_event = EventHandle::new().context("Failed creating WSA event")?;

            // SAFETY: WSAEventSelect() requires a valid socket and WSA event, which are both
            // passed here, and the bitflag of events that should be selected for.
            //
            // On error a non-zero value is returned.
            unsafe {
                if WSAEventSelect(event_socket.as_raw_socket(), event_socket_event.0, FD_READ) != 0
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

            Ok(Self {
                event_socket,
                event_socket_event,
                general_socket,
                general_socket_event,
                stdin,
                stdout,
            })
        }

        /// Mutable reference to the event socket.
        pub fn event_socket(&mut self) -> &mut UdpSocket {
            &mut self.event_socket
        }

        /// Mutable reference to the general socket.
        pub fn general_socket(&mut self) -> &mut UdpSocket {
            &mut self.general_socket
        }

        /// Mutable reference to stdin for reading.
        pub fn stdin(&mut self) -> &mut Stdin {
            &mut self.stdin
        }

        /// Mutable reference to stdout for writing.
        pub fn stdout(&mut self) -> &mut Stdout {
            &mut self.stdout
        }

        /// Poll the event socket, general socket and stdin for available data to read.
        ///
        /// This blocks until at least one input has data available.
        pub fn poll(&mut self) -> Result<PollResult, Error> {
            let handles = [
                self.event_socket_event.0,
                self.general_socket_event.0,
                // If stdin is a pipe then we use the signalling event, otherwise stdin itself.
                if let Some(ref thread_state) = self.stdin.thread_state {
                    thread_state.event
                } else {
                    self.stdin.handle
                },
            ];

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
                let res =
                    WaitForMultipleObjects(handles.len() as _, handles[..].as_ptr(), 0, u32::MAX);
                if res == u32::MAX {
                    bail!(
                        source: io::Error::from_raw_os_error(WSAGetLastError()),
                        "Failed waiting for events"
                    );
                }

                assert!(
                    (0..=2).contains(&res),
                    "Unexpected WaitForMultipleObjects() return value {}",
                    res,
                );

                res
            };

            // For the sockets, enumerate the events that woke up the waiting, collect any errors
            // and reset the event objects.
            if (0..=1).contains(&res) {
                let (socket, event) = if res == 0 {
                    (&self.event_socket, &self.event_socket_event)
                } else {
                    (&self.general_socket, &self.general_socket_event)
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
                            "Failed enumerating network events on {} socket",
                            if res == 0 { "event" } else { "general" },
                        );
                    }

                    networkevents.assume_init()
                };

                if networkevents.ierrorcode[FD_READ_BIT] != 0 {
                    bail!(
                        source: io::Error::from_raw_os_error(networkevents.ierrorcode[FD_READ_BIT]),
                        "Error on {} socket while waiting for events",
                        if res == 0 { "event" } else { "general" },
                    );
                }
                assert!(networkevents.lnetworkevents & FD_READ != 0);
            }

            Ok(PollResult {
                event_socket: res == 0,
                general_socket: res == 1,
                stdin: res == 2,
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
                } else if handle as isize == -1 {
                    bail!(source: io::Error::last_os_error(), "Can't get stdin handle");
                }

                handle
            };
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
        fn read(&mut self, mut buf: &mut [u8]) -> io::Result<usize> {
            if buf.is_empty() {
                return Ok(0);
            }

            // If a read byte is pending from the readiness signalling thread then
            // read that first here before reading any remaining data.
            let mut already_read = 0;
            if let Some(ref mut thread_state) = self.thread_state {
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
                } else if handle as isize == -1 {
                    bail!(
                        source: io::Error::last_os_error(),
                        "Can't get stdout handle"
                    );
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
                bail!("Unsupported stdout handle type {:x}", type_);
            }

            Ok(Stdout(handle))
        }
    }

    impl Write for Stdout {
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
}

pub use self::imp::{Poll, PollResult, Stdin, Stdout};
