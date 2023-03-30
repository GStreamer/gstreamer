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
    net::{Ipv4Addr, SocketAddr, UdpSocket},
};

mod args;
mod error;
mod ffi;
mod io;
mod net;
mod privileges;
mod rand;
mod thread;

use error::{Context, Error};
use rand::rand;

/// PTP Multicast group.
const PTP_MULTICAST_ADDR: Ipv4Addr = Ipv4Addr::new(224, 0, 1, 129);
/// PTP Event message port.
const PTP_EVENT_PORT: u16 = 319;
/// PTP General message port.
const PTP_GENERAL_PORT: u16 = 320;

/// Create a new `UdpSocket` for the given port and configure it for PTP.
fn create_socket(port: u16) -> Result<UdpSocket, Error> {
    let socket = UdpSocket::bind(SocketAddr::from((Ipv4Addr::UNSPECIFIED, port)))
        .with_context(|| format!("Failed to bind socket to port {}", port))?;

    socket.set_ttl(1).context("Failed setting TTL on socket")?;
    socket
        .set_multicast_ttl_v4(1)
        .context("Failed to set multicast TTL on socket")?;

    net::set_reuse(&socket);

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

            false
        });

        if ifaces.is_empty() {
            bail!("None of the selected network interfaces found");
        }
        if ifaces.len() != args.interfaces.len() {
            bail!("Not all selected network interfaces found");
        }
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

    Ok(clock_id)
}

fn main() -> Result<(), Error> {
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
        let mut clock_id_data = [0u8; 4 + 8];
        clock_id_data[0..2].copy_from_slice(&8u16.to_le_bytes());
        clock_id_data[2] = 2;
        clock_id_data[3] = 0;
        clock_id_data[4..].copy_from_slice(&clock_id);

        poll.stdout()
            .write_all(&clock_id_data)
            .context("Failed writing to stdout")?;
    }

    // Now read-write from stdin/stdout and the sockets
    //
    // We assume that stdout never blocks and stdin receives a complete valid packet whenever it is
    // ready and never blocks in the middle of a packet.
    let mut socket_buffer = [0u8; 1500];
    let mut stdinout_buffer = [0u8; 1504];

    loop {
        let poll_res = poll.poll().context("Failed polling")?;

        // If any of the sockets are ready, continue reading packets from them until no more
        // packets are left and directly forward them to stdout.
        'next_socket: for idx in [poll_res.event_socket, poll_res.general_socket]
            .iter()
            .enumerate()
            .filter_map(|(idx, r)| if *r { Some(idx) } else { None })
        {
            let res = match idx {
                0 => poll.event_socket().recv(&mut socket_buffer),
                1 => poll.general_socket().recv(&mut socket_buffer),
                _ => unreachable!(),
            };

            match res {
                Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                    continue 'next_socket;
                }
                Err(err) => {
                    bail!(
                        source: err,
                        "Failed reading from {} socket",
                        if idx == 0 { "event" } else { "general" }
                    );
                }
                Ok(read) => {
                    stdinout_buffer[0..2].copy_from_slice(&(read as u16).to_ne_bytes());
                    stdinout_buffer[2] = idx as u8;
                    stdinout_buffer[3] = 0;
                    stdinout_buffer[4..][..read].copy_from_slice(&socket_buffer[..read]);

                    poll.stdout()
                        .write_all(&stdinout_buffer[..(read + 4)])
                        .context("Failed writing to stdout")?;
                }
            }
        }

        // After handling the sockets check if a packet is available on stdin, read it and forward
        // it to the corresponding socket.
        if poll_res.stdin {
            poll.stdin()
                .read_exact(&mut stdinout_buffer[0..4])
                .context("Failed reading packet header from stdin")?;

            let size = u16::from_ne_bytes([stdinout_buffer[0], stdinout_buffer[1]]);
            if size as usize > stdinout_buffer.len() {
                bail!("Invalid packet size on stdin {}", size);
            }
            let type_ = stdinout_buffer[2];

            poll.stdin()
                .read_exact(&mut stdinout_buffer[0..size as usize])
                .context("Failed reading packet body from stdin")?;

            let buf = &stdinout_buffer[0..size as usize];
            match type_ {
                0 => poll
                    .event_socket()
                    .send_to(buf, (PTP_MULTICAST_ADDR, PTP_EVENT_PORT)),
                1 => poll
                    .general_socket()
                    .send_to(buf, (PTP_MULTICAST_ADDR, PTP_GENERAL_PORT)),
                _ => unreachable!(),
            }
            .with_context(|| {
                format!(
                    "Failed sending to {} socket",
                    if type_ == 0 { "event" } else { "general" }
                )
            })?;
        }
    }
}
