// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use std::env;

use crate::{
    bail,
    error::{Context, Error},
};

/// Parsed command-line arguments.
#[derive(Debug)]
#[allow(dead_code)]
pub struct Args {
    pub interfaces: Vec<String>,
    pub verbose: bool,
    pub clock_id: u64,
    pub ttl: u32,
    /// Named pipe path for stdin replacement (Windows named pipe IPC).
    pub pipe_in: Option<String>,
    /// Named pipe path for stdout replacement (Windows named pipe IPC).
    pub pipe_out: Option<String>,
    /// Named pipe path for stderr replacement (Windows named pipe IPC).
    pub pipe_err: Option<String>,
}

/// Parse the command-line arguments.
pub fn parse_args() -> Result<Args, Error> {
    let mut interfaces = Vec::new();
    let mut verbose = false;
    let mut clock_id = 0;
    let mut ttl = 1;
    let mut pipe_in = None;
    let mut pipe_out = None;
    let mut pipe_err = None;

    let mut args = env::args();
    // Skip executable name
    let _ = args.next();

    while let Some(arg) = args.next() {
        match arg.as_str() {
            "-v" | "--verbose" => {
                verbose = true;
            }
            "-i" | "--interface" => {
                let iface = args.next().context("No interface following -i")?;
                interfaces.push(iface);
            }
            "-c" | "--clock-id" => {
                let clock_id_arg = args.next().context("No clock-id following -c")?;
                if !clock_id_arg.starts_with("0x") && !clock_id_arg.starts_with("0X") {
                    bail!("Clock ID not starting with 0x");
                }
                clock_id =
                    u64::from_str_radix(&clock_id_arg[2..], 16).context("Invalid clock ID")?;
            }
            "--ttl" => {
                let ttl_arg = args.next().context("No TTL following --ttl")?;
                ttl = ttl_arg.parse::<u32>().context("Invalid TTL value")?;
            }
            "--pipe-in" => {
                pipe_in = Some(args.next().context("No pipe path following --pipe-in")?);
            }
            "--pipe-out" => {
                pipe_out = Some(args.next().context("No pipe path following --pipe-out")?);
            }
            "--pipe-err" => {
                pipe_err = Some(args.next().context("No pipe path following --pipe-err")?);
            }

            arg => {
                bail!("Unknown command-line argument {}", arg);
            }
        }
    }

    let args = Args {
        interfaces,
        verbose,
        clock_id,
        ttl,
        pipe_in,
        pipe_out,
        pipe_err,
    };

    info!("Running with arguments {:#?}", args);

    Ok(args)
}
