//! `pm-parser` — Rust port of the Platinenmacher parsing and text helpers.
//!
//! Replaces these C files:
//!
//! | C file                                  | Rust module        |
//! |-----------------------------------------|--------------------|
//! | `lib/Platinenmacher/parser/command.{c,h}` | [`command`]      |
//! | `lib/Platinenmacher/parser/config.{c,h}`  | [`config`]       |
//! | `lib/helper/readline.c`                   | [`readline`]     |
//! | `lib/helper/printfb.c`                    | [`printfb`]      |
//!
//! # Scope: no I/O, no concurrency
//!
//! Everything in this crate is pure computation over borrowed strings and
//! slices, so it builds and unit-tests on the host.
//!
//! Two concerns that the C code mixes in here deliberately do *not* live in
//! this crate:
//!
//! * The `save_sprintf` / `save_snprintf` print-semaphore macros in
//!   `include/tasks.h`. Those serialise access to the console across FreeRTOS
//!   tasks; that is firmware-runtime behaviour. This crate only *formats* (see
//!   [`printfb`]) and hands the caller a `String`/`core::fmt::Write` sink — the
//!   firmware crate owns the semaphore and the actual write.
//! * The `putchar()` echo inside `command_parser()` and the `printf()` calls
//!   inside `printf_fb()`. [`command::CommandParser::parse`] never touches
//!   stdout; echoing is the serial task's job.

pub mod command;
pub mod config;
pub mod error;
pub mod printfb;
pub mod readline;

pub use command::{CommandHandler, CommandParser, Invocation};
pub use error::Error;
pub use printfb::format_fb;
pub use readline::{countline, readline};
