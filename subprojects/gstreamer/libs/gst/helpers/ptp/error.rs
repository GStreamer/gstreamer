// GStreamer
//
// Copyright (C) 2015-2023 Sebastian Dröge <sebastian@centricular.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// <https://mozilla.org/MPL/2.0/>.
//
// SPDX-License-Identifier: MPL-2.0

use std::{
    error::Error as StdError,
    fmt::{Debug, Display},
};

/// Custom error type for error display reasons.
pub struct Error(Box<ErrorInner>);

impl Error {
    #[doc(hidden)]
    pub fn new(message: String, source: Option<Box<dyn StdError + 'static>>) -> Self {
        Error(Box::new(ErrorInner { message, source }))
    }
}

struct ErrorInner {
    message: String,
    source: Option<Box<dyn StdError + 'static>>,
}

impl Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut e = self;
        let mut first = true;

        // Print the actual error message of this error and then iterate over the whole
        // error chain and print each cause on the chain indented.
        'next_error: loop {
            if first {
                writeln!(f, "{}", e.0.message)?;
                first = false;
            } else {
                for line in e.0.message.lines() {
                    writeln!(f, "  {}", line)?;
                }
            }

            let mut source = match e.0.source {
                Some(ref source) => &**source,
                None => break 'next_error,
            };

            if let Some(source) = source.downcast_ref::<Error>() {
                e = source;
                writeln!(f, "\nCaused by:\n")?;
                continue 'next_error;
            }

            loop {
                writeln!(f, "\nCaused by:\n")?;
                let source_str = source.to_string();
                for line in source_str.lines() {
                    writeln!(f, "  {}", line)?;
                }

                source = match source.source() {
                    None => break 'next_error,
                    Some(source) => source,
                };

                if let Some(source) = source.downcast_ref::<Error>() {
                    e = source;
                    writeln!(f, "\nCaused by:\n")?;
                    continue 'next_error;
                }
            }
        }

        Ok(())
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        <Self as Debug>::fmt(self, f)
    }
}

impl StdError for Error {
    fn source(&self) -> Option<&(dyn StdError + 'static)> {
        match self.0.source {
            None => None,
            Some(ref source) => Some(&**source),
        }
    }
}

impl<'a> From<&'a str> for Error {
    fn from(message: &'a str) -> Self {
        Error(Box::new(ErrorInner {
            message: String::from(message),
            source: None,
        }))
    }
}

impl From<String> for Error {
    fn from(message: String) -> Self {
        Error(Box::new(ErrorInner {
            message,
            source: None,
        }))
    }
}

#[macro_export]
/// Create a new `Error` from the given message and possibly source error.
macro_rules! format_err {
    (source: $source:expr, $msg:literal $(,)?) => {
        $crate::error::Error::new(
            String::from($msg),
            Some($source.into()),
        )
    };
    (source: $source:expr, $err:expr $(,)?) => {
        $crate::error::Error::new(
            format!($err),
            Some($source.into()),
        )
    };
    (source: $source:expr, $fmt:expr, $($arg:tt)*) => {
        $crate::error::Error::new(
            format!($fmt, $($arg)*),
            Some($source.into()),
        )
    };

    ($msg:literal $(,)?) => {
        $crate::error::Error::new(
            String::from($msg),
            None,
        )
    };
    ($err:expr $(,)?) => {
        $crate::error::Error::new(
            format!($err),
            None,
        )
    };
    ($fmt:expr, $($arg:tt)*) => {
        $crate::error::Error::new(
            format!($fmt, $($arg)*),
            None,
        )
    };
}

#[macro_export]
/// Return new `Error` from the given message and possibly source error.
macro_rules! bail {
    ($($arg:tt)+) => {
        return Err($crate::format_err!($($arg)+));
    };
}

/// Trait for adding a context message to any `Result<T, E>` or `Option<T>`
/// and turning it into a `Result<T, Error>`.
pub trait Context<T, E> {
    /// Add a static context.
    ///
    /// This should only be called if `context` requires no allocations or otherwise
    /// exists already.
    fn context<C>(self, context: C) -> Result<T, Error>
    where
        C: Display;

    /// Add a lazily created context.
    fn with_context<C, F>(self, func: F) -> Result<T, Error>
    where
        C: Display,
        F: FnOnce() -> C;
}

impl<T, E> Context<T, E> for Result<T, E>
where
    E: StdError + 'static,
{
    fn context<C>(self, context: C) -> Result<T, Error>
    where
        C: Display,
    {
        self.map_err(|err| Error::new(context.to_string(), Some(Box::new(err))))
    }

    fn with_context<C, F>(self, func: F) -> Result<T, Error>
    where
        C: Display,
        F: FnOnce() -> C,
    {
        self.map_err(|err| Error::new(func().to_string(), Some(Box::new(err))))
    }
}

impl<T> Context<T, Error> for Option<T> {
    fn context<C>(self, context: C) -> Result<T, Error>
    where
        C: Display,
    {
        self.ok_or_else(|| Error::new(context.to_string(), None))
    }

    fn with_context<C, F>(self, func: F) -> Result<T, Error>
    where
        C: Display,
        F: FnOnce() -> C,
    {
        self.ok_or_else(|| Error::new(func().to_string(), None))
    }
}
