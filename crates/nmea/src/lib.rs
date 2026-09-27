/*
 * NMEA parsing for the Quectel L96 GNSS module
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! NMEA parsing for the Quectel L96 GNSS module, ported from
//! `lib/nmea_parser`.
//!
//! The vendor sentences the L96 answers with are handled by the [`plugin`]
//! layer: a [`SentencePlugin`] per command set, registered with a
//! [`PluginRegistry`] that the [`dispatch`] module consults for every sentence
//! the core does not recognise itself. [`l96`] holds the module's command set.
//!
//! ```
//! use nmea::{l96, Dispatcher, PluginRegistry, PmtkPlugin, PqPlugin, Statement};
//!
//! let mut pmtk = PmtkPlugin::new();
//! let mut pq = PqPlugin::new();
//! let mut registry = PluginRegistry::new();
//! // `src/esp32/gps.c` registers PMTK first and PQ second.
//! registry.register(&mut pmtk).unwrap();
//! registry.register(&mut pq).unwrap();
//! let mut dispatcher = Dispatcher::new(registry);
//!
//! assert_eq!(
//!     dispatcher.feed_sentence(l96::REPLY_GLP),
//!     Ok(Statement::Plugin(1)),
//! );
//! ```

#![cfg_attr(not(test), no_std)]
#![deny(missing_docs)]

pub mod buf;
pub mod checksum;
pub mod dispatch;
pub mod error;
pub mod l96;
pub mod plugin;

pub use checksum::{checksum, verify};
pub use dispatch::{classify, sentence_items, Dispatcher, Statement, STATEMENT_PLUGIN};
pub use error::{Error, Result};
pub use plugin::pmtk::{PmtkCommand, PmtkPlugin};
pub use plugin::pq::{PqPlugin, PqStatement};
pub use plugin::{PluginRegistry, SentenceItem, SentencePlugin, GPS_MAX_PARSER_PLUGINS};
