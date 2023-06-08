// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

//! Helper process that runs setuid root or with appropriate privileges to
//! listen on ports < 1024, do multicast operations and get MAC addresses of
//! interfaces. Privileges are dropped after these operations are done.
//!
//! It listens on the PTP multicast group on port 319 and 320 and forwards
//! everything received there to stdout, while forwarding everything received
//! on stdin to those sockets.
//! Additionally it provides the MAC address of a network interface via stdout

use std::{
    io::{Read, Write},
    net::{Ipv4Addr, UdpSocket},
};

#[macro_use]
mod log;

mod args;
mod clock;
mod error;
mod ffi;
mod io;
mod net;
mod parse;
mod privileges;
mod rand;
mod thread;

use error::{Context, Error};
use parse::{PtpClockIdentity, PtpMessagePayload, ReadBytesBEExt, WriteBytesBEExt};
use rand::rand;

/// PTP Multicast group.
const PTP_MULTICAST_ADDR: Ipv4Addr = Ipv4Addr::new(224, 0, 1, 129);
/// PTP Event message port.
const PTP_EVENT_PORT: u16 = 319;
/// PTP General message port.
const PTP_GENERAL_PORT: u16 = 320;

/// StdIO Message Types.
/// PTP message for the event socket.
const MSG_TYPE_EVENT: u8 = 0;
/// PTP message for the general socket.
const MSG_TYPE_GENERAL: u8 = 1;
/// Clock ID message
const MSG_TYPE_CLOCK_ID: u8 = 2;
/// Send time ACK message
const MSG_TYPE_SEND_TIME_ACK: u8 = 3;

/// Create a new `UdpSocket` for the given port and configure it for PTP.
fn create_socket(port: u16) -> Result<UdpSocket, Error> {
    let socket = net::create_udp_socket(&Ipv4Addr::UNSPECIFIED, port)
        .with_context(|| format!("Failed to bind socket to port {}", port))?;

    socket
        .set_nonblocking(true)
        .context("Failed setting socket non-blocking")?;
    socket.set_ttl(1).context("Failed setting TTL on socket")?;
    socket
        .set_multicast_ttl_v4(1)
        .context("Failed to set multicast TTL on socket")?;

    Ok(socket)
}

/// Join the multicast groups for PTP on the configured interfaces.
fn join_multicast(
    args: &args::Args,
    event_socket: &UdpSocket,
    general_socket: &UdpSocket,
) -> Result<[u8; 8], Error> {
    let mut ifaces = net::query_interfaces().context("Failed to query network interfaces")?;
    if ifaces.is_empty() {
        bail!("No suitable network interfaces for PTP found");
    }

    if !args.interfaces.is_empty() {
        ifaces.retain(|iface| {
            for filter_iface in &args.interfaces {
                if &iface.name == filter_iface {
                    return true;
                }
                if let Some(ref other_name) = iface.other_name {
                    if other_name == filter_iface {
                        return true;
                    }
                }
                if let Ok(addr) = filter_iface.parse::<Ipv4Addr>() {
                    if addr == iface.ip_addr {
                        return true;
                    }
                }
            }

            info!("Interface {} filtered out", iface.name);

            false
        });

        if ifaces.is_empty() {
            bail!("None of the selected network interfaces found");
        }
        if ifaces.len() != args.interfaces.len() {
            bail!("Not all selected network interfaces found");
        }
    }

    for iface in &ifaces {
        info!("Binding to interface {}", iface.name);
    }

    for socket in [&event_socket, &general_socket].iter() {
        for iface in &ifaces {
            net::join_multicast_v4(socket, &PTP_MULTICAST_ADDR, iface)
                .context("Failed to join multicast group")?;
        }
    }

    let clock_id = if args.clock_id == 0 {
        ifaces
            .iter()
            .find_map(|iface| iface.hw_addr)
            .map(|hw_addr| {
                [
                    hw_addr[0], hw_addr[1], hw_addr[2], 0xff, 0xfe, hw_addr[3], hw_addr[4],
                    hw_addr[5],
                ]
            })
            .unwrap_or_else(rand)
    } else {
        args.clock_id.to_be_bytes()
    };

    info!("Using clock ID {:?}", clock_id);

    Ok(clock_id)
}

fn run() -> Result<(), Error> {
    let args = args::parse_args().context("Failed parsing commandline parameters")?;

    let event_socket = create_socket(PTP_EVENT_PORT).context("Failed creating event socket")?;
    let general_socket =
        create_socket(PTP_GENERAL_PORT).context("Failed creating general socket")?;

    thread::set_priority().context("Failed to set thread priority")?;

    privileges::drop().context("Failed dropping privileges")?;

    let clock_id = join_multicast(&args, &event_socket, &general_socket)
        .context("Failed joining multicast groups")?;

    let mut poll = io::Poll::new(event_socket, general_socket).context("Failed creating poller")?;

    // Write clock ID first
    {
        let mut clock_id_data = [0u8; 3 + 8];
        {
            let mut buf = &mut clock_id_data[..];
            buf.write_u16be(8).expect("Too small clock ID buffer");
            buf.write_u8(MSG_TYPE_CLOCK_ID)
                .expect("Too small clock ID buffer");
            buf.write_all(&clock_id).expect("Too small clock ID buffer");
            assert!(buf.is_empty(), "Too big clock ID buffer");
        }

        poll.stdout()
            .write_all(&clock_id_data)
            .context("Failed writing to stdout")?;
    }

    // Now read-write from stdin/stdout and the sockets
    //
    // We assume that stdout never blocks and stdin receives a complete valid packet whenever it is
    // ready and never blocks in the middle of a packet.
    let mut socket_buffer = [0u8; 8192];
    let mut stdinout_buffer = [0u8; 8192 + 3 + 8];

    loop {
        let poll_res = poll.poll().context("Failed polling")?;

        // If any of the sockets are ready, continue reading packets from them until no more
        // packets are left and directly forward them to stdout.
        'next_socket: for idx in [poll_res.event_socket, poll_res.general_socket]
            .iter()
            .enumerate()
            .filter_map(|(idx, r)| if *r { Some(idx) } else { None })
        {
            let idx = idx as u8;

            // Read all available packets from the socket before going to the next socket.
            'next_packet: loop {
                let res = match idx {
                    MSG_TYPE_EVENT => poll.event_socket().recv_from(&mut socket_buffer),
                    MSG_TYPE_GENERAL => poll.general_socket().recv_from(&mut socket_buffer),
                    _ => unreachable!(),
                };

                let (read, addr) = match res {
                    Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                        continue 'next_socket;
                    }
                    Err(err) => {
                        bail!(
                            source: err,
                            "Failed reading from {} socket",
                            if idx == MSG_TYPE_EVENT {
                                "event"
                            } else {
                                "general"
                            }
                        );
                    }
                    Ok((read, addr)) => (read, addr),
                };

                let recv_time = clock::time();
                if args.verbose {
                    trace!(
                        "Received {} bytes from {} socket from {} at {}",
                        read,
                        if idx == MSG_TYPE_EVENT {
                            "event"
                        } else {
                            "general"
                        },
                        addr,
                        recv_time,
                    );
                }

                let buf = &socket_buffer[..read];

                // Check if this is a valid PTP message, that it is not a PTP message sent from
                // our own clock ID and in case of a DELAY_RESP that it is for our clock id.
                let ptp_message = match parse::PtpMessage::parse(buf) {
                    Ok(msg) => msg,
                    Err(err) => {
                        warn!("Received invalid PTP message: {}", err);
                        continue 'next_packet;
                    }
                };

                if args.verbose {
                    trace!("Received PTP message {:#?}", ptp_message);
                }

                if ptp_message.source_port_identity.clock_identity == u64::from_be_bytes(clock_id) {
                    if args.verbose {
                        trace!("Ignoring our own PTP message");
                    }
                    continue 'next_packet;
                }
                if let PtpMessagePayload::DelayResp {
                    requesting_port_identity: PtpClockIdentity { clock_identity, .. },
                    ..
                } = ptp_message.message_payload
                {
                    if clock_identity != u64::from_be_bytes(clock_id) {
                        if args.verbose {
                            trace!("Ignoring PTP DELAY_RESP message for a different clock");
                        }
                        continue 'next_packet;
                    }
                }

                {
                    let mut buf = &mut stdinout_buffer[..(read + 3 + 8)];
                    buf.write_u16be(read as u16 + 8)
                        .expect("Too small stdout buffer");
                    buf.write_u8(idx).expect("Too small stdout buffer");
                    buf.write_u64be(recv_time).expect("Too small stdout buffer");
                    buf.write_all(&socket_buffer[..read])
                        .expect("Too small stdout buffer");
                    assert!(buf.is_empty(), "Too big stdout buffer",);
                }

                let buf = &stdinout_buffer[..(read + 3 + 8)];
                poll.stdout()
                    .write_all(buf)
                    .context("Failed writing to stdout")?;
            }
        }

        // After handling the sockets check if a packet is available on stdin, read it and forward
        // it to the corresponding socket.
        if poll_res.stdin {
            poll.stdin()
                .read_exact(&mut stdinout_buffer[0..3])
                .context("Failed reading packet header from stdin")?;

            let size = u16::from_be_bytes([stdinout_buffer[0], stdinout_buffer[1]]);
            if size as usize > stdinout_buffer.len() {
                bail!("Invalid packet size on stdin {}", size);
            }
            let type_ = stdinout_buffer[2];

            poll.stdin()
                .read_exact(&mut stdinout_buffer[0..size as usize])
                .context("Failed reading packet body from stdin")?;

            if type_ != MSG_TYPE_EVENT && type_ != MSG_TYPE_GENERAL {
                warn!("Unexpected stdin message type {}", type_);
                continue;
            }

            if args.verbose {
                trace!(
                    "Received {} bytes for {} socket from stdin",
                    size,
                    if type_ == MSG_TYPE_EVENT {
                        "event"
                    } else {
                        "general"
                    },
                );
            }

            if size < 8 + 34 {
                bail!("Invalid packet body size");
            }

            let buf = &mut &stdinout_buffer[..(size as usize)];
            let main_send_time = buf.read_u64be().expect("Too small stdin buffer");

            // We require that the main process only ever sends valid PTP messages with the clock
            // ID assigned by this process.
            let ptp_message =
                parse::PtpMessage::parse(buf).context("Parsing PTP message from main process")?;
            if ptp_message.source_port_identity.clock_identity != u64::from_be_bytes(clock_id) {
                bail!("PTP message with unexpected clock identity on stdin");
            }

            if args.verbose {
                trace!("Received PTP message from stdin {:#?}", ptp_message);
            }

            let send_time = clock::time();
            match type_ {
                MSG_TYPE_EVENT => poll
                    .event_socket()
                    .send_to(buf, (PTP_MULTICAST_ADDR, PTP_EVENT_PORT)),
                MSG_TYPE_GENERAL => poll
                    .general_socket()
                    .send_to(buf, (PTP_MULTICAST_ADDR, PTP_GENERAL_PORT)),
                _ => unreachable!(),
            }
            .with_context(|| {
                format!(
                    "Failed sending to {} socket",
                    if type_ == MSG_TYPE_EVENT {
                        "event"
                    } else {
                        "general"
                    }
                )
            })?;

            if args.verbose {
                trace!(
                    "Sending SEND_TIME_ACK for message type {}, domain number {}, seqnum {} received at {} at {}",
                    u8::from(ptp_message.message_type),
                    ptp_message.domain_number,
                    ptp_message.sequence_id,
                    main_send_time,
                    send_time,
                );
            }

            {
                let mut buf = &mut stdinout_buffer[..(3 + 12)];
                buf.write_u16be(12).expect("Too small stdout buffer");
                buf.write_u8(MSG_TYPE_SEND_TIME_ACK)
                    .expect("Too small stdout buffer");
                buf.write_u64be(send_time).expect("Too small stdout buffer");
                buf.write_u8(ptp_message.message_type.into())
                    .expect("Too small stdout buffer");
                buf.write_u8(ptp_message.domain_number)
                    .expect("Too small stdout buffer");
                buf.write_u16be(ptp_message.sequence_id)
                    .expect("Too small stdout buffer");
                assert!(buf.is_empty(), "Too big stdout buffer",);
            }

            let buf = &stdinout_buffer[..(3 + 12)];
            poll.stdout()
                .write_all(buf)
                .context("Failed writing to stdout")?;
        }
    }
}

/// Custom panic hook so we can print them to stderr in a format the main process understands
fn panic_hook(info: &std::panic::PanicInfo) {
    error!("Panicked. {}", info);
}

fn main() {
    std::panic::set_hook(Box::new(panic_hook));

    if let Err(err) = run() {
        error!("Exited with error: {:?}", err);
    }
}
