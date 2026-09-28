//! Port of `lib/Platinenmacher/parser/command.{c,h}` — the serial/console
//! command parser.
//!
//! The C version is a character-at-a-time state machine over two global heap
//! buffers, with a fixed table of `command_t` structs holding a
//! `void (*function_cb)(struct command *)`. Registered handlers are things
//! like `render_cmd_cb()` in `src/esp32/gui.c`.
//!
//! What the port keeps:
//!
//! * The state machine, byte for byte: `\r` dispatches, the first space inside
//!   a command switches to argument collection, later spaces are part of the
//!   arguments, a leading space in [`State::Idle`] is skipped.
//! * Unknown commands dispatch to entry 0, the "unknown command" slot that
//!   `command_init()` registers before anything else.
//! * `CMD_MAX` caps the table *including* that unknown slot, so only
//!   [`CMD_MAX`] − 1 real commands fit. Registering past it fails with
//!   [`Error::OutOfBounds`], as `command_register()` does.
//! * The command name is capped at [`CMD_LENGTH`] and the arguments at
//!   [`CMD_ARGS_LENGTH`].
//!
//! What the port deliberately changes:
//!
//! * **No echo.** `command_parser()` calls `putchar()` on every byte it
//!   consumes. Console I/O (and the `save_printf` semaphore that guards it)
//!   belongs to the firmware serial task, not to a parser — see the crate
//!   docs. [`CommandParser::parse`] is pure.
//! * **No overflow.** The C parser writes past the end of its `CMD_LENGTH + 1`
//!   and `CMD_ARGS_LENGTH + 1` buffers when a line is longer, corrupting the
//!   heap. Here the excess characters are dropped. An over-long name therefore
//!   arrives truncated and matches nothing, dispatching to the unknown handler.
//! * **No `strcmp(NULL, ..)`.** In C the unknown entry is a zeroed global, so
//!   `find_command()` compares against a `NULL` name on every lookup, which is
//!   undefined behaviour. Here the unknown entry simply has no name and never
//!   participates in matching.
//! * The handler receives the command name **as typed**, not the name stored
//!   in the table. For a matched command those are the same string; for an
//!   unknown command C passed a `NULL` name, and the typed text is strictly
//!   more useful.
//!
//! A quirk that is *preserved* because callers depend on `\r` framing: `\n` is
//! not a terminator, it is an ordinary character. Feeding `"cmd\r\n"` leaves
//! the `\n` as the first character of the *next* command. The firmware feeds
//! `\r`-terminated console lines, so this only bites on CRLF input.
//!
//! ```
//! use pm_parser::{CommandParser, Invocation};
//! use std::cell::Cell;
//!
//! let hits = Cell::new(0);
//! let mut parser = CommandParser::new();
//! parser
//!     .register("render", |_: &Invocation<'_>| hits.set(hits.get() + 1))
//!     .unwrap();
//! parser.parse("render\r");
//! assert_eq!(hits.get(), 1);
//! ```

use crate::error::Error;

/// `CMD_MAX` from `command.h`: table size, unknown-command slot included.
pub const CMD_MAX: usize = 10;
/// `CMD_LENGTH` from `command.h`: longest command name that can be parsed.
pub const CMD_LENGTH: usize = 10;
/// `CMD_ARGS_LENGTH` from `command.h`: longest argument string.
pub const CMD_ARGS_LENGTH: usize = 70;

/// Port of `cmd_t`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    /// `CMD_IDLE`: skipping leading spaces.
    Idle,
    /// `CMD_COMMAND`: collecting the command name.
    Command,
    /// `CMD_ARGS`: collecting the argument string.
    Args,
}

/// What a handler is given when its command fires.
///
/// Replaces the `command_t *` that C handed to `function_cb`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Invocation<'a> {
    /// The command name as typed (already truncated to [`CMD_LENGTH`]).
    pub command: &'a str,
    /// Everything after the first space, truncated to [`CMD_ARGS_LENGTH`].
    /// Empty when the command was given without arguments.
    pub args: &'a str,
}

/// Typed replacement for `void (*function_cb)(struct command *cmd)`.
///
/// Implemented for every `FnMut(&Invocation<'_>)`, so plain closures work.
pub trait CommandHandler {
    /// Run the command.
    fn call(&mut self, invocation: &Invocation<'_>);
}

impl<F> CommandHandler for F
where
    F: FnMut(&Invocation<'_>),
{
    fn call(&mut self, invocation: &Invocation<'_>) {
        self(invocation)
    }
}

struct Entry<'a> {
    /// `None` for the unknown-command slot, which never matches.
    name: Option<String>,
    handler: Box<dyn CommandHandler + 'a>,
}

/// The command table plus the parser state machine.
///
/// Replaces the file-scope globals of `command.c` (`cmd_list`, `max_cmd`,
/// `state`, `cmd_buf`, `args_buf`), so several parsers can coexist and tests
/// do not share state.
pub struct CommandParser<'a> {
    /// Entry 0 is always the unknown-command slot, as after `command_init()`.
    entries: Vec<Entry<'a>>,
    state: State,
    cmd: String,
    args: String,
}

impl Default for CommandParser<'_> {
    fn default() -> Self {
        Self::new()
    }
}

impl<'a> CommandParser<'a> {
    /// Port of `command_init()`: empty buffers, state `CMD_COMMAND`, and the
    /// unknown-command slot registered as entry 0.
    ///
    /// The unknown handler does nothing, matching C's zeroed `unknown_cmd`
    /// whose `function_cb` is `NULL`. Use [`CommandParser::set_unknown_handler`]
    /// to give it behaviour.
    pub fn new() -> Self {
        Self {
            entries: vec![Entry {
                name: None,
                handler: Box::new(|_: &Invocation<'_>| {}),
            }],
            // command_init() leaves the machine in CMD_COMMAND, so State::Idle
            // is only reachable by an explicit reset; it is kept for fidelity.
            state: State::Command,
            cmd: String::with_capacity(CMD_LENGTH),
            args: String::with_capacity(CMD_ARGS_LENGTH),
        }
    }

    /// Replace the handler for unrecognised commands (entry 0).
    pub fn set_unknown_handler(&mut self, handler: impl CommandHandler + 'a) {
        self.entries[0].handler = Box::new(handler);
    }

    /// Port of `command_register()`.
    ///
    /// Fails with [`Error::OutOfBounds`] once the table holds [`CMD_MAX`]
    /// entries — the unknown-command slot counts, exactly as `max_cmd` does
    /// in C.
    ///
    /// Duplicate names are accepted and the first registration wins, because
    /// `find_command()` returns the first match. A name longer than
    /// [`CMD_LENGTH`] is accepted too, and can never match, because the parser
    /// truncates what it reads — both are C behaviour.
    pub fn register(
        &mut self,
        name: &str,
        handler: impl CommandHandler + 'a,
    ) -> Result<(), Error> {
        if self.entries.len() >= CMD_MAX {
            return Err(Error::OutOfBounds);
        }
        self.entries.push(Entry {
            name: Some(name.to_owned()),
            handler: Box::new(handler),
        });
        Ok(())
    }

    /// Number of registered commands, the unknown-command slot excluded.
    pub fn len(&self) -> usize {
        self.entries.len() - 1
    }

    /// `true` when no command beyond the unknown-command slot is registered.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Current state machine position.
    pub fn state(&self) -> State {
        self.state
    }

    /// Port of `command_parser(char *buf, uint32_t len)`, minus the echo.
    pub fn parse(&mut self, input: &str) {
        for c in input.chars() {
            self.parse_char(c);
        }
    }

    /// Port of `command_parse_char()`.
    pub fn parse_char(&mut self, c: char) {
        if c == '\r' {
            // C sets state before dispatching, so the next line starts in
            // CMD_COMMAND even if this one ended mid-arguments.
            self.state = State::Command;
            self.dispatch();
            return;
        }

        match self.state {
            State::Idle => {
                if c == ' ' {
                    return;
                }
                // C falls through from CMD_IDLE into CMD_COMMAND: the
                // character that ended the idle run is part of the command.
                self.state = State::Command;
                self.push_cmd(c);
            }
            State::Command => {
                if c == ' ' {
                    self.state = State::Args;
                } else {
                    self.push_cmd(c);
                }
            }
            // Everything after the first space is arguments, spaces included.
            State::Args => self.push_args(c),
        }
    }

    /// Drop the half-parsed line and return to the post-`command_init()` state.
    pub fn reset(&mut self) {
        self.state = State::Command;
        self.cmd.clear();
        self.args.clear();
    }

    /// Port of `find_command()` + `run_command()`.
    fn dispatch(&mut self) {
        // C: `find_command()` returns 0 — the unknown slot — when nothing
        // matches, and the unknown slot itself has no name to match against.
        let idx = self
            .entries
            .iter()
            .position(|e| e.name.as_deref() == Some(self.cmd.as_str()))
            .unwrap_or(0);

        let invocation = Invocation {
            command: self.cmd.as_str(),
            args: self.args.as_str(),
        };
        // Disjoint field borrows: the handler is borrowed mutably out of
        // `entries` while the buffers are borrowed immutably.
        self.entries[idx].handler.call(&invocation);

        // C: run_command() zeroes both buffers afterwards.
        self.cmd.clear();
        self.args.clear();
    }

    fn push_cmd(&mut self, c: char) {
        // C overflows its CMD_LENGTH + 1 byte buffer here; we drop the excess
        // instead. The limit is counted in bytes, as in C, so a multi-byte
        // character is dropped whole rather than split.
        if self.cmd.len() + c.len_utf8() <= CMD_LENGTH {
            self.cmd.push(c);
        }
    }

    fn push_args(&mut self, c: char) {
        if self.args.len() + c.len_utf8() <= CMD_ARGS_LENGTH {
            self.args.push(c);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;

    /// Records every dispatch as `(command, args)` so tests can assert on the
    /// exact strings the C callbacks would have seen.
    #[derive(Default)]
    struct Log(RefCell<Vec<(String, String)>>);

    impl Log {
        fn record(&self, i: &Invocation<'_>) {
            self.0
                .borrow_mut()
                .push((i.command.to_owned(), i.args.to_owned()));
        }
        fn take(&self) -> Vec<(String, String)> {
            self.0.borrow_mut().drain(..).collect()
        }
    }

    #[test]
    fn dispatches_two_registered_commands() {
        let render = Log::default();
        let reboot = Log::default();
        let mut parser = CommandParser::new();
        parser.register("render", |i: &Invocation<'_>| render.record(i)).unwrap();
        parser.register("reboot", |i: &Invocation<'_>| reboot.record(i)).unwrap();

        parser.parse("render\rreboot now\r");

        assert_eq!(render.take(), vec![("render".to_string(), String::new())]);
        assert_eq!(reboot.take(), vec![("reboot".to_string(), "now".to_string())]);
    }

    #[test]
    fn unknown_command_falls_back_to_entry_zero() {
        let known = Log::default();
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));
        parser.register("render", |i: &Invocation<'_>| known.record(i)).unwrap();

        parser.parse("nope arg\r");

        assert!(known.take().is_empty());
        assert_eq!(unknown.take(), vec![("nope".to_string(), "arg".to_string())]);
    }

    #[test]
    fn empty_line_dispatches_the_unknown_handler() {
        // C: find_command("") matches nothing, so entry 0 runs with empty args.
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));

        parser.parse("\r");

        assert_eq!(unknown.take(), vec![(String::new(), String::new())]);
    }

    #[test]
    fn default_unknown_handler_is_a_noop() {
        // C's unknown_cmd has a NULL function_cb; run_command() skips the call.
        let mut parser = CommandParser::new();
        parser.parse("whatever\r");
        assert_eq!(parser.state(), State::Command);
    }

    #[test]
    fn arguments_keep_every_space_after_the_first() {
        let log = Log::default();
        let mut parser = CommandParser::new();
        parser.register("set", |i: &Invocation<'_>| log.record(i)).unwrap();

        parser.parse("set  a b \r");

        // The first space switches to CMD_ARGS and is swallowed; the rest are
        // copied verbatim.
        assert_eq!(log.take(), vec![("set".to_string(), " a b ".to_string())]);
    }

    #[test]
    fn buffers_are_cleared_between_commands() {
        let log = Log::default();
        let mut parser = CommandParser::new();
        parser.register("set", |i: &Invocation<'_>| log.record(i)).unwrap();

        parser.parse("set value\rset\r");

        assert_eq!(
            log.take(),
            vec![
                ("set".to_string(), "value".to_string()),
                ("set".to_string(), String::new()),
            ]
        );
    }

    #[test]
    fn over_long_command_is_truncated_and_unknown() {
        let known = Log::default();
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));
        parser
            .register("0123456789abc", |i: &Invocation<'_>| known.record(i))
            .unwrap();

        parser.parse("0123456789abc\r");

        // CMD_LENGTH characters survive; the registered 13-char name is
        // unreachable, exactly as in C (where the tail smashed the heap).
        assert!(known.take().is_empty());
        assert_eq!(unknown.take(), vec![("0123456789".to_string(), String::new())]);
    }

    #[test]
    fn over_long_arguments_are_truncated() {
        let log = Log::default();
        let mut parser = CommandParser::new();
        parser.register("x", |i: &Invocation<'_>| log.record(i)).unwrap();

        let long = "a".repeat(CMD_ARGS_LENGTH + 20);
        parser.parse(&format!("x {long}\r"));

        let recorded = log.take();
        assert_eq!(recorded.len(), 1);
        assert_eq!(recorded[0].1.len(), CMD_ARGS_LENGTH);
    }

    #[test]
    fn truncation_counts_bytes_and_never_splits_a_character() {
        // C's buffers are CMD_LENGTH + 1 *bytes*, so the cap is in bytes here
        // too — but a multi-byte character is dropped whole, never halved.
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));

        // Six 'ä' are 12 bytes; only five fit in CMD_LENGTH = 10.
        parser.parse("ääääää\r");

        let recorded = unknown.take();
        assert_eq!(recorded, vec![("äääää".to_string(), String::new())]);
        assert_eq!(recorded[0].0.len(), CMD_LENGTH);
    }

    #[test]
    fn registration_is_capped_at_cmd_max_including_the_unknown_slot() {
        let mut parser = CommandParser::new();
        for i in 0..CMD_MAX - 1 {
            parser
                .register(&format!("c{i}"), |_: &Invocation<'_>| {})
                .unwrap();
        }
        assert_eq!(parser.len(), CMD_MAX - 1);
        assert_eq!(
            parser.register("one_more", |_: &Invocation<'_>| {}),
            Err(Error::OutOfBounds)
        );
    }

    #[test]
    fn first_registration_of_a_duplicate_name_wins() {
        let first = Log::default();
        let second = Log::default();
        let mut parser = CommandParser::new();
        parser.register("dup", |i: &Invocation<'_>| first.record(i)).unwrap();
        parser.register("dup", |i: &Invocation<'_>| second.record(i)).unwrap();

        parser.parse("dup\r");

        assert_eq!(first.take().len(), 1);
        assert!(second.take().is_empty());
    }

    #[test]
    fn newline_is_an_ordinary_character() {
        // Faithful to C: only '\r' dispatches, so CRLF leaves the '\n' at the
        // head of the next command.
        let log = Log::default();
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));
        parser.register("render", |i: &Invocation<'_>| log.record(i)).unwrap();

        parser.parse("render\r\nrender\r");

        assert_eq!(log.take().len(), 1);
        assert_eq!(unknown.take(), vec![("\nrender".to_string(), String::new())]);
    }

    #[test]
    fn idle_state_skips_leading_spaces() {
        let log = Log::default();
        let mut parser = CommandParser::new();
        parser.register("render", |i: &Invocation<'_>| log.record(i)).unwrap();

        parser.reset_to_idle_for_test();
        parser.parse("   render\r");

        assert_eq!(log.take(), vec![("render".to_string(), String::new())]);
    }

    #[test]
    fn command_state_does_not_skip_leading_spaces() {
        // In CMD_COMMAND the first space switches to CMD_ARGS even if no
        // command characters were seen yet: " render" is an empty command with
        // "render" as its arguments.
        let unknown = Log::default();
        let mut parser = CommandParser::new();
        parser.set_unknown_handler(|i: &Invocation<'_>| unknown.record(i));

        parser.parse(" render\r");

        assert_eq!(unknown.take(), vec![(String::new(), "render".to_string())]);
    }

    #[test]
    fn reset_drops_a_half_parsed_line() {
        let log = Log::default();
        let mut parser = CommandParser::new();
        parser.register("render", |i: &Invocation<'_>| log.record(i)).unwrap();

        parser.parse("ren");
        parser.reset();
        parser.parse("render\r");

        assert_eq!(log.take(), vec![("render".to_string(), String::new())]);
    }

    impl CommandParser<'_> {
        /// `State::Idle` is unreachable through the public API because
        /// `command_init()` starts in `CMD_COMMAND`; this puts the machine
        /// there so the idle branch stays covered.
        fn reset_to_idle_for_test(&mut self) {
            self.reset();
            self.state = State::Idle;
        }
    }
}
