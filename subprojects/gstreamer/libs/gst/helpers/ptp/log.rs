// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

#[cfg(not(test))]
use crate::io::Stderr;
#[cfg(not(test))]
use std::io::{self, Cursor, Write};

#[derive(Copy, Clone, PartialEq, Eq)]
#[repr(u8)]
#[allow(dead_code)]
pub enum LogLevel {
    Error = 1,
    Warning = 2,
    _Fixme = 3,
    Info = 4,
    Debug = 5,
    _Log = 6,
    Trace = 7,
}

/*
 * - 1 byte GstDebugLevel
 * - 2 byte BE filename length
 * - filename UTF-8 string
 * - 2 byte BE module path length
 * - module path UTF-8 string
 * - 4 byte BE line number
 * - remainder is UTF-8 string
*/
#[cfg(test)]
pub fn log(
    _level: LogLevel,
    _file: &str,
    _module_path: &str,
    _line: u32,
    _args: std::fmt::Arguments,
) {
}

#[cfg(not(test))]
pub fn log(level: LogLevel, file: &str, module_path: &str, line: u32, args: std::fmt::Arguments) {
    let mut stderr = Stderr::acquire();
    let mut buffer = [0u8; 8192];
    let mut cursor = Cursor::new(&mut buffer[..]);

    // Silently ignore errors. What was written to the buffer was written and if there's more data
    // than fits it will simply be cut off in the end.
    let _ = (|| -> Result<(), io::Error> {
        cursor.write_all(&[0u8, 0u8])?;
        cursor.write_all(&[level as u8])?;
        cursor.write_all(&(file.len() as u16).to_be_bytes())?;
        cursor.write_all(file.as_bytes())?;
        cursor.write_all(&(module_path.len() as u16).to_be_bytes())?;
        cursor.write_all(module_path.as_bytes())?;
        cursor.write_all(&line.to_be_bytes())?;
        cursor.write_fmt(args)?;

        Ok(())
    })();

    let pos = cursor.position() as u16;
    if pos < 2 {
        return;
    }

    cursor.set_position(0);
    let _ = cursor.write_all(&(pos - 2).to_be_bytes());

    let _ = stderr.write_all(&buffer[..pos as usize]);
}

#[allow(unused_macros)]
macro_rules! error {
    ($format:expr $(, $arg:expr)* $(,)?) => {{
        $crate::log::log($crate::log::LogLevel::Error, file!(), module_path!(), line!(), format_args!($format, $($arg),*));
    }};
}
#[allow(unused_macros)]
macro_rules! warn {
    ($format:expr $(, $arg:expr)* $(,)?) => {{
        $crate::log::log($crate::log::LogLevel::Warning, file!(), module_path!(), line!(), format_args!($format, $($arg),*));
    }};
}
#[allow(unused_macros)]
macro_rules! info {
    ($format:expr $(, $arg:expr)* $(,)?) => {{
        $crate::log::log($crate::log::LogLevel::Info, file!(), module_path!(), line!(), format_args!($format, $($arg),*));
    }};
}
#[allow(unused_macros)]
macro_rules! debug {
    ($format:expr $(, $arg:expr)* $(,)?) => {{
        $crate::log::log($crate::log::LogLevel::Debug, file!(), module_path!(), line!(), format_args!($format, $($arg),*));
    }};
}
#[allow(unused_macros)]
macro_rules! trace {
    ($format:expr $(, $arg:expr)* $(,)?) => {{
        $crate::log::log($crate::log::LogLevel::Trace, file!(), module_path!(), line!(), format_args!($format, $($arg),*));
    }};
}
