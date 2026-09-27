/*
 * Error type replacing esp_err_t in the ported parsers
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! The parser error type.

/// The subset of `esp_err_t` the C parsers in `lib/nmea_parser` return.
///
/// | C                       | Rust                     |
/// |-------------------------|--------------------------|
/// | `ESP_OK`                | `Ok(())`                 |
/// | `ESP_ERR_NOT_SUPPORTED` | `Err(Error::NotSupported)` |
/// | `ESP_ERR_INVALID_ARG`   | `Err(Error::InvalidArg)` |
/// | `ESP_FAIL`              | `Err(Error::Failed)`     |
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The sentence or packet type is not handled: `ESP_ERR_NOT_SUPPORTED`.
    ///
    /// A plugin returns this from [`SentencePlugin::detect`] to decline a
    /// sentence, and from [`SentencePlugin::parse`] when the receiver reported
    /// an unsupported packet type.
    ///
    /// [`SentencePlugin::detect`]: crate::plugin::SentencePlugin::detect
    /// [`SentencePlugin::parse`]: crate::plugin::SentencePlugin::parse
    NotSupported,
    /// The receiver reported an invalid packet: `ESP_ERR_INVALID_ARG`.
    InvalidArg,
    /// The receiver accepted the packet but the action failed: `ESP_FAIL`.
    Failed,
    /// No free plugin slot left; see [`GPS_MAX_PARSER_PLUGINS`].
    ///
    /// The C code has no equivalent: it writes plugins into a fixed array in
    /// `nmea_parser_config_t` and silently ignores anything past the end.
    ///
    /// [`GPS_MAX_PARSER_PLUGINS`]: crate::plugin::GPS_MAX_PARSER_PLUGINS
    RegistryFull,
}

/// Result alias for the parser, mirroring the C functions returning `esp_err_t`.
pub type Result<T> = core::result::Result<T, Error>;
